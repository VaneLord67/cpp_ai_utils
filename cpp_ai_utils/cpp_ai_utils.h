#pragma once
#include "cpp_redis/cpp_redis"
#include "opencv2/opencv.hpp"
#include "spdlog/spdlog.h"
#include <iostream>
#include <fstream>

#ifdef _WIN32
#include <Winsock2.h>
#endif /* _WIN32 */

namespace cpp_ai_utils {

	enum class SourceType { IMAGE, VIDEO, CAMERA };

	class CppAiHelper {

	private:
		std::string m_queueName;
		std::string m_stopSignalKey;
		std::string m_logKey;
		std::string m_videoOutputPath;
		std::string m_videoProgressKey;
		std::string m_outputJsonPath;
		std::shared_ptr<std::ofstream> m_jsonFilePtr;
		std::shared_ptr<cv::VideoWriter> m_videoWriterPtr; // 使用指针进行延迟初始化
		int m_totalFrameCount;
		int m_currentFrameCount;

		SourceType m_sourceType;
		cpp_redis::client m_redisClient;
		
	public:

		CppAiHelper(const std::string logKey, 
			const std::string queueName=std::string(), 
			const std::string stopSignalKey=std::string(),
			const std::string videoOutputPath=std::string(),
			const std::string videoProgressKey=std::string(),
			const std::string videoOutputJsonPath=std::string());

		std::shared_ptr<CppAiHelper> create_cpp_ai_helper_by_command_arg(int argc, char** argv);

		~CppAiHelper();

		

		void init_video_writer(const cv::VideoCapture& cap);
		void write_frame_to_video(const cv::Mat& frame);
		void write_json_to_file(const std::string& jsonStr);
		void push_log_to_redis(const std::string& logStr);
		bool should_stop_camera();
		void push_frame_to_redis(const cv::Mat& frame);
		void push_str_to_redis(const std::string& str);
		cpp_redis::client& get_redis_client() { return this->m_redisClient; };
		SourceType get_source_type() { return this->m_sourceType; };
	};

}
