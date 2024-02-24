#include "pch.h"
#include "framework.h"
#include "cpp_ai_utils.h"

using namespace cpp_ai_utils;

CppAiHelper::CppAiHelper(const std::string logKey, 
	const std::string queueName, 
	const std::string stopSignalKey, 
	const std::string videoOutputPath,
	const std::string videoProgressKey,
	const std::string videoOutputJsonPath,
	const std::string videoPath):

	m_queueName(queueName), m_stopSignalKey(stopSignalKey), m_logKey(logKey), 
	m_videoOutputPath(videoOutputPath), m_videoProgressKey(videoProgressKey), m_outputJsonPath(videoOutputJsonPath),
	m_videoWriterPtr(nullptr), m_totalFrameCount(0), m_currentFrameCount(0), 
	m_sourceType(cpp_ai_utils::SourceType::IMAGE), m_videoPath(videoPath) {

#ifdef _WIN32
	//! Windows netword DLL init
	WORD version = MAKEWORD(2, 2);
	WSADATA data;

	if (WSAStartup(version, &data) != 0) {
		spdlog::error("WSAStartup() failure");
		spdlog::shutdown();
		exit(-1);
	}
#endif /* _WIN32 */

	m_redisClient.connect("127.0.0.1", 6379, [](const std::string& host, std::size_t port, cpp_redis::client::connect_state status) {
		if (status == cpp_redis::client::connect_state::dropped) {
			spdlog::info("client disconnected from {}:{}", host, port);
		}
	});

	if (!m_outputJsonPath.empty()) {
		m_jsonFilePtr.reset(new std::ofstream(m_outputJsonPath, std::ios::app));
		if (!m_jsonFilePtr || !(m_jsonFilePtr->is_open())) {
			spdlog::error("Failed to create json file!");
			spdlog::shutdown();
			exit(-1);
		}
	}

	if (!m_queueName.empty()) {
		m_sourceType = cpp_ai_utils::SourceType::CAMERA;
	} else if (!m_videoOutputPath.empty()) {
		m_sourceType = cpp_ai_utils::SourceType::VIDEO;
	}
}

std::shared_ptr<CppAiHelper> CppAiHelper::create_cpp_ai_helper_by_command_arg(int argc, char** argv) {
	cv::CommandLineParser parser(argc, argv,
		{
			"{video||video's path}"
			"{cam_id||camera's device id}"
			"{img||image's path}"
			"{queueName|| camera jpg data queue   }"
			"{stopSignalKey|| stop camera signal key  }"
			"{logKey||log key}"
			"{videoOutputPath||video output path}"
			"{videoProgressKey||video progress key}"
			"{videoOutputJsonPath||video output json path}"
		});
	std::string imagePath = "";
	std::string videoPath = "";
	int cameraId = 0;
	std::string queueName = "";
	std::string stopSignalKey = "";
	std::string logKey = "";
	std::string videoOutputPath = "";
	std::string videoProgressKey = "";
	std::string videoOutputJsonPath = "";

	if (parser.has("img")) {
		imagePath = parser.get<std::string>("img");
		spdlog::info("image path = {}", imagePath);
		m_sourceType = SourceType::IMAGE;
	}
	if (parser.has("video")) {
		videoPath = parser.get<std::string>("video");
		spdlog::info("video path = {}", videoPath);
		m_sourceType = SourceType::VIDEO;
	}
	if (parser.has("cam_id")) {
		cameraId = parser.get<int>("cam_id");
		spdlog::info("camera id = {}", cameraId);
		m_sourceType = SourceType::CAMERA;
	}
	if (parser.has("queueName")) {
		queueName = parser.get<std::string>("queueName");
		spdlog::info("queueName = {}", queueName);
	}
	if (parser.has("stopSignalKey")) {
		stopSignalKey = parser.get<std::string>("stopSignalKey");
		spdlog::info("stopSignalKey = {}", stopSignalKey);
	}
	if (parser.has("logKey")) {
		logKey = parser.get<std::string>("logKey");
		spdlog::info("logKey = {}", logKey);
	}
	if (parser.has("videoOutputPath")) {
		videoOutputPath = parser.get<std::string>("videoOutputPath");
		spdlog::info("videoOutputPath = {}", videoOutputPath);
	}
	if (parser.has("videoProgressKey")) {
		videoProgressKey = parser.get<std::string>("videoProgressKey");
		spdlog::info("videoProgressKey = {}", videoProgressKey);
	}
	if (parser.has("videoOutputJsonPath")) {
		videoOutputJsonPath = parser.get<std::string>("videoOutputJsonPath");
		spdlog::info("videoOutputJsonPath = {}", videoOutputJsonPath);
	}
	auto ptr = std::shared_ptr<CppAiHelper>(new CppAiHelper(logKey, queueName, stopSignalKey,
		videoOutputPath, videoProgressKey, videoOutputJsonPath));
	return ptr;
}

CppAiHelper::~CppAiHelper() {
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

	spdlog::info("destruct CppAiHelper success");
}

void CppAiHelper::push_log_to_redis(const std::string& logStr) {
	m_redisClient.rpush(m_logKey, { logStr });
	m_redisClient.sync_commit();
}

bool CppAiHelper::should_stop_camera() {
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
		spdlog::error("Failed to get stop signal: {}", reply.as_string());
	}
	return false;
}

void CppAiHelper::push_frame_to_redis(const cv::Mat& frame) {
	if (m_queueName.empty()) {
		spdlog::error("queueName not found!");
		return;
	}
	if (frame.empty()) {
		spdlog::error("frame is empty!");
		return;
	}
	std::vector<uchar> jpg_buffer;
	cv::imencode(".jpg", frame, jpg_buffer);
	std::string jpg_data(reinterpret_cast<const char*>(jpg_buffer.data()), jpg_buffer.size());

	auto future = m_redisClient.rpush(m_queueName, { jpg_data });
	m_redisClient.commit();
	auto reply = future.get();
	if (reply.is_string()) {
		spdlog::error("Failed to push data to the redis. Error: {}", reply.as_string());
	}
}

void CppAiHelper::push_str_to_redis(const std::string& str) {
	if (m_queueName.empty()) {
		spdlog::error("queueName not found!");
		return;
	}
	m_redisClient.rpush(m_queueName, { str });
	m_redisClient.sync_commit();
}

int CppAiHelper::manual_get_total_frame_count() {
	if (m_videoPath.empty()) {
		spdlog::error("videoPath not found!");
		return;
	}
	cv::Mat frame;
	int total_frame_count = 0;
	cv::VideoCapture cap(m_videoPath);
	if (!cap.isOpened()) {
		spdlog::error("Failed to manual get total frame count!");
		return total_frame_count;
	}
	while (true) {
		cap >> frame; // 读取下一帧
		if (frame.empty()) // 如果已经到达视频结尾，则退出循环
			break;
		total_frame_count++;
	}
	return total_frame_count;
}

void CppAiHelper::init_video_writer(const cv::VideoCapture& cap) {
	if (m_sourceType == SourceType::VIDEO) {
		if (m_videoProgressKey.empty()) {
			spdlog::error("videoProgressKey not found!");
			return;
		}
		m_totalFrameCount = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT));
		if (m_totalFrameCount <= 0) {
			spdlog::info("manual to get video total frame count");
			m_totalFrameCount = manual_get_total_frame_count();
		}
		if (m_totalFrameCount <= 0) {
			spdlog::error("Failed to get video total frame count!");
			return;
		}
		m_redisClient.setex(m_videoProgressKey, 3600 * 24, "0.00");
		m_redisClient.commit();
	}
	if (m_videoOutputPath.empty()) {
		spdlog::error("videoOutputPath not found!");
		return;
	}
	int frameWidth = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
	int frameHeight = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
	double fps = 0;
	if (m_sourceType == SourceType::VIDEO) {
		fps = cap.get(cv::CAP_PROP_FPS);
	}
	else if (m_sourceType == SourceType::CAMERA) {
		fps = 30;
	}
	spdlog::info("frameWidth {}, frameHeight {}, fps {}", frameWidth, frameHeight, fps);

	m_videoWriterPtr.reset(new cv::VideoWriter(m_videoOutputPath,
		cv::VideoWriter::fourcc('a', 'v', 'c', '1'), fps, cv::Size(frameWidth, frameHeight)));

	spdlog::info("init videoWriter success");
}

void CppAiHelper::write_frame_to_video(const cv::Mat& frame) {
	if (!m_videoWriterPtr) {
		spdlog::error("Failed to get videoWriter, please init video writer.");
		return;
	}
	if (!frame.empty()) {
		m_videoWriterPtr->write(frame);
	}
	else {
		spdlog::error("write_frame_to_video: frame is empty!");
	}
	if (m_sourceType == SourceType::VIDEO) {
		m_currentFrameCount++;
		if (m_currentFrameCount > m_totalFrameCount) {
			spdlog::error("currentFrameCount > totalFrameCount!");
			m_currentFrameCount = m_totalFrameCount;
		}
		double progress = static_cast<double>(m_currentFrameCount) / m_totalFrameCount;
		std::string progressStr = cv::format("%.2f", progress);
		m_redisClient.setex(m_videoProgressKey, 3600 * 24, progressStr);
		m_redisClient.commit();
	}
}

void CppAiHelper::write_json_to_file(const std::string& jsonStr) {
	if (m_outputJsonPath.empty()) {
		spdlog::error("json path not found!");
		return;
	}
	if (!m_jsonFilePtr) {
		spdlog::error("Failed to write json file!");
		return;
	}
	*m_jsonFilePtr << jsonStr << std::endl;
}
