#include "pch.h"
#include "framework.h"
#include "cpp_ai_utils.h"
#include <iostream>
#include <fstream>

cpp_ai_utils::CppAiHelper::CppAiHelper(const std::string logKey, 
	const std::string queueName, 
	const std::string stopSignalKey, 
	const std::string videoOutputPath,
	const std::string videoProgressKey,
	const std::string videoOutputJsonPath):

	m_queueName(queueName), m_stopSignalKey(stopSignalKey), m_logKey(logKey), 
	m_videoOutputPath(videoOutputPath), m_videoProgressKey(videoProgressKey), m_outputJsonPath(videoOutputJsonPath),
	m_videoWriterPtr(nullptr), m_totalFrameCount(0), m_currentFrameCount(0), 
	m_sourceType(cpp_ai_utils::SourceType::IMAGE) {

#ifdef _WIN32
	//! Windows netword DLL init
	WORD version = MAKEWORD(2, 2);
	WSADATA data;

	if (WSAStartup(version, &data) != 0) {
		std::cerr << "WSAStartup() failure" << std::endl;
		exit(-1);
	}
#endif /* _WIN32 */

	m_redisClient.connect("127.0.0.1", 6379, [](const std::string& host, std::size_t port, cpp_redis::client::connect_state status) {
		if (status == cpp_redis::client::connect_state::dropped) {
			std::cout << "client disconnected from " << host << ":" << port << std::endl;
		}
	});

	if (!m_outputJsonPath.empty()) {
		m_jsonFilePtr.reset(new std::ofstream(m_outputJsonPath, std::ios::app));
		if (!m_jsonFilePtr || !(m_jsonFilePtr->is_open())) {
			std::cerr << "Failed to create json file!" << std::endl;
			return;
		}
	}

	if (!m_queueName.empty()) {
		m_sourceType = cpp_ai_utils::SourceType::CAMERA;
	} else if (!m_videoOutputPath.empty()) {
		m_sourceType = cpp_ai_utils::SourceType::VIDEO;
	}
}

cpp_ai_utils::CppAiHelper::~CppAiHelper() {
	if (m_redisClient.is_reconnecting()) {
		m_redisClient.cancel_reconnect();
	}
	if (m_redisClient.is_connected()) {
		m_redisClient.disconnect();
	}
	if (m_videoWriterPtr) {
		m_videoWriterPtr->release();
	}
	if (m_jsonFilePtr) {
		m_jsonFilePtr->close();
	}

#ifdef _WIN32
	WSACleanup();
#endif /* _WIN32 */
}

void cpp_ai_utils::CppAiHelper::push_log_to_redis(const std::string& logStr) {
	m_redisClient.rpush(m_logKey, { logStr });
	m_redisClient.sync_commit();
}

bool cpp_ai_utils::CppAiHelper::should_stop_camera() {
	auto future = m_redisClient.exists({ m_stopSignalKey });
	m_redisClient.commit();
	auto reply = future.get();
	if (reply.is_integer() && reply.as_integer() == 1) {
		std::cout << "receive stop signal, shutdown program..." << std::endl;
		m_redisClient.rpush(m_queueName, { "stop" });
		m_redisClient.expire(m_queueName, 60);
		m_redisClient.sync_commit();

		return true;
	}
	if (reply.is_string()) {
		std::cerr << "Failed to get stop signal: " << reply.as_string() << std::endl;
	}
	return false;
}

void cpp_ai_utils::CppAiHelper::push_frame_to_redis(const cv::Mat& frame) {
	if (m_queueName.empty()) {
		std::cerr << "queueName not found!" << std::endl;
		return;
	}
	if (frame.empty()) {
		std::cerr << "frame is empty!" << std::endl;
		return;
	}
	std::vector<uchar> jpg_buffer;
	cv::imencode(".jpg", frame, jpg_buffer);
	std::string jpg_data(reinterpret_cast<const char*>(jpg_buffer.data()), jpg_buffer.size());

	auto future = m_redisClient.rpush(m_queueName, { jpg_data });
	m_redisClient.commit();
	auto reply = future.get();
	if (reply.is_string()) {
		std::cerr << "Failed to push data to the redis. Error: " << reply.as_string() << std::endl;
	}
}

void cpp_ai_utils::CppAiHelper::push_str_to_redis(const std::string& str) {
	if (m_queueName.empty()) {
		std::cerr << "queueName not found!" << std::endl;
		return;
	}
	m_redisClient.rpush(m_queueName, { str });
	m_redisClient.sync_commit();
}

void cpp_ai_utils::CppAiHelper::init_video_writer(const cv::VideoCapture& cap) {
	if (m_videoProgressKey.empty()) {
		std::cerr << "videoProgressKey not found!" << std::endl;
		return;
	}
	if (m_videoOutputPath.empty()) {
		std::cerr << "videoOutputPath not found!" << std::endl;
		return;
	}
	int frameWidth = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
	int frameHeight = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
	double fps = cap.get(cv::CAP_PROP_FPS);

	m_videoWriterPtr.reset(new cv::VideoWriter(m_videoOutputPath,
		cv::VideoWriter::fourcc('a', 'v', 'c', '1'), fps, cv::Size(frameWidth, frameHeight)));

	m_totalFrameCount = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT));
	if (m_totalFrameCount == 0) {
		std::cerr << "Failed to get video total frame count!" << std::endl;
		return;
	}
	m_redisClient.setex(m_videoProgressKey, 3600 * 24,"0.00");
	m_redisClient.commit();
}

void cpp_ai_utils::CppAiHelper::write_frame_to_video(const cv::Mat& frame) {
	if (!m_videoWriterPtr) {
		std::cerr << "Failed to get videoWriter, please init video writer." << std::endl;
		return;
	}
	if (!frame.empty()) {
		m_videoWriterPtr->write(frame);
	}
	m_currentFrameCount++;
	if (m_currentFrameCount > m_totalFrameCount) {
		std::cerr << "currentFrameCount > totalFrameCount!" << std::endl;
		m_currentFrameCount = m_totalFrameCount;
	}
	double progress = static_cast<double>(m_currentFrameCount) / m_totalFrameCount;
	std::string progressStr = cv::format("%.2f", progress);
	m_redisClient.setex(m_videoProgressKey, 3600 * 24, progressStr);
	m_redisClient.commit();
}

void cpp_ai_utils::CppAiHelper::write_json_to_file(const std::string& jsonStr) {
	if (m_outputJsonPath.empty()) {
		std::cerr << "json path not found!" << std::endl;
		return;
	}
	if (!m_jsonFilePtr) {
		std::cerr << "Failed to write json file!" << std::endl;
		return;
	}
	*m_jsonFilePtr << jsonStr << std::endl;
}
