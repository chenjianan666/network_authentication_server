#include <iostream>
#include <thread>
#include <vector>
#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include "MysqlClient.h"
#include "HttpParser.h"
#include <random>
#include "jsoncpp/json.h"
DECLARE_TABLE_CLASS(edoyunLogin_user_mysql, _mysql_table_)
DECLARE_MYSQL_FIELD(TYPE_INT, user_id, NOT_NULL | PRIMARY_KEY | AUTOINCREMENT, "INTEGER", "", "", "")
DECLARE_MYSQL_FIELD(TYPE_TEXT, user_name, NOT_NULL, "TEXT", "", "", "")    //姓名
DECLARE_MYSQL_FIELD(TYPE_VARCHAR, user_qq, NOT_NULL, "VARCHAR", "(15)", "", "")  //QQ号
DECLARE_MYSQL_FIELD(TYPE_VARCHAR, user_phone, DEFAULT, "VARCHAR", "(11)", "", "")  //手机
DECLARE_MYSQL_FIELD(TYPE_DATETIME, expiration_date, DEFAULT, "DATETIME", "", "", "")
DECLARE_MYSQL_FIELD(TYPE_TEXT, sign, NOT_NULL, "TEXT", "", "", "")

//DECLARE_MYSQL_FIELD(TYPE_TEXT, user_nick, NOT_NULL, "TEXT", "", "", "")    //昵称
//DECLARE_MYSQL_FIELD(TYPE_TEXT, user_wechat, DEFAULT, "TEXT", "", "NULL", "")
//DECLARE_MYSQL_FIELD(TYPE_TEXT, user_wechat_id, DEFAULT, "TEXT", "", "NULL", "")
//DECLARE_MYSQL_FIELD(TYPE_TEXT, user_address, DEFAULT, "TEXT", "", "", "")
//DECLARE_MYSQL_FIELD(TYPE_TEXT, user_province, DEFAULT, "TEXT", "", "", "")
//DECLARE_MYSQL_FIELD(TYPE_TEXT, user_country, DEFAULT, "TEXT", "", "", "")
//DECLARE_MYSQL_FIELD(TYPE_INT, user_age, DEFAULT | CHECK, "INTEGER", "", "18", "")
//DECLARE_MYSQL_FIELD(TYPE_INT, user_male, DEFAULT, "BOOL", "", "1", "")
//DECLARE_MYSQL_FIELD(TYPE_TEXT, user_flags, DEFAULT, "TEXT", "", "0", "")
//DECLARE_MYSQL_FIELD(TYPE_REAL, user_experience, DEFAULT, "REAL", "", "0.0", "")
//DECLARE_MYSQL_FIELD(TYPE_INT, user_level, DEFAULT | CHECK, "INTEGER", "", "0", "")
//DECLARE_MYSQL_FIELD(TYPE_TEXT, user_class_priority, DEFAULT, "TEXT", "", "", "")
//DECLARE_MYSQL_FIELD(TYPE_REAL, user_time_per_viewer, DEFAULT, "REAL", "", "", "")
//DECLARE_MYSQL_FIELD(TYPE_TEXT, user_career, NONE, "TEXT", "", "", "")
//DECLARE_MYSQL_FIELD(TYPE_TEXT, user_password, NOT_NULL, "TEXT", "", "", "")
//DECLARE_MYSQL_FIELD(TYPE_INT, user_birthday, NONE, "DATETIME", "", "", "")
//DECLARE_MYSQL_FIELD(TYPE_TEXT, user_describe, NONE, "TEXT", "", "", "")
//DECLARE_MYSQL_FIELD(TYPE_TEXT, user_education, NONE, "TEXT", "", "", "")
//DECLARE_MYSQL_FIELD(TYPE_INT, user_register_time, DEFAULT, "DATETIME", "", "LOCALTIME()", "")
DECLARE_TABLE_CLASS_EDN()

DECLARE_TABLE_CLASS(keyvalue_mysql, _mysql_table_)
DECLARE_MYSQL_FIELD(TYPE_VARCHAR, KeyValue, DEFAULT, "VARCHAR", "(32)", "'18888888888'", "")
DECLARE_MYSQL_FIELD(TYPE_INT, DayType, DEFAULT, "INTEGER", "", "", "")
DECLARE_MYSQL_FIELD(TYPE_BOOL, IsUsed, DEFAULT, "BOOL", "", "", "")
DECLARE_TABLE_CLASS_EDN()
class ThreadPool {
public:
	explicit ThreadPool(size_t numThreads) : stop(false) {
		for (size_t i = 0; i < numThreads; ++i) {
			workers.push_back(std::thread([this] {
				while (true) {
					std::function<void()> task;
					{
						std::unique_lock<std::mutex> lock(this->queueMutex);
						this->cv.wait(lock, [this] { return this->stop || !this->tasks.empty(); });
						if (this->stop && this->tasks.empty())
							return;
						task = std::move(this->tasks.front());
						this->tasks.pop();
					}
					task();
				}
				}));
		}
	}

	~ThreadPool() {
		{
			std::unique_lock<std::mutex> lock(queueMutex);
			stop = true;
		}
		cv.notify_all();
		for (std::thread& worker : workers) {
			worker.join();
		}
	}

	void enqueue(std::function<void()> f) {
		{
			std::unique_lock<std::mutex> lock(queueMutex);
			tasks.push(f);
		}
		cv.notify_one();
	}

private:
	std::vector<std::thread> workers;
	std::queue<std::function<void()>> tasks;
	std::mutex queueMutex;
	std::condition_variable cv;
	bool stop;
};
CDatabaseClient* m_db;
Buffer gk = "";
// 处理HTTP请求

std::string getCurrentTime() {
	printf("获取当前时间\n");
	auto now = std::chrono::system_clock::now();
	std::time_t now_c = std::chrono::system_clock::to_time_t(now);
	std::tm* localTime = std::localtime(&now_c);

	char buffer[20];
	std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", localTime);
	printf("最终返回字符串%s\n", buffer);
	return std::string(buffer);
}
std::string addDaysToCurrentDate(int dayType) {
	printf("给当前日期添加增量\n");
	// 获取当前时间
	std::time_t now = std::time(nullptr);
	std::tm* timeInfo = std::localtime(&now);

	// 加上 DayType 天
	timeInfo->tm_mday += dayType;
	std::mktime(timeInfo);  // 标准化时间结构

	// 手动格式化时间为字符串
	char buffer[20];
	std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeInfo);
	printf("最终返回字符串%s\n",buffer);

	return buffer;
}
std::string addDaysToDateTime(const std::string& datetimeStr, int days) {
	printf("给目标时间添加增量\n");
	// 手动解析字符串，将 "YYYY-MM-DD HH:MM:SS" 格式转为 std::tm
	std::tm tm = {};
	int year, month, day, hour, minute, second;

	// 使用 sscanf 来解析日期时间字符串
	if (sscanf(datetimeStr.c_str(), "%d-%d-%d %d:%d:%d",
		&year, &month, &day, &hour, &minute, &second) != 6) {
		throw std::runtime_error("Failed to parse DateTime string");
	}

	// 填充 tm 结构
	tm.tm_year = year - 1900; // 年份从1900开始计数
	tm.tm_mon = month - 1;    // 月份从0开始计数
	tm.tm_mday = day;
	tm.tm_hour = hour;
	tm.tm_min = minute;
	tm.tm_sec = second;

	// 转换为 time_t 类型
	std::time_t time = std::mktime(&tm);
	// 增加天数（以秒为单位）
	time += days * 24 * 60 * 60;

	// 将 time_t 转回 tm 结构
	std::tm* newTm = std::localtime(&time);

	// 格式化输出字符串
	char buffer[20];
	std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", newTm);
	printf("最终返回字符串%s\n", buffer);
	return buffer;
}
std::string generateKey(int length = 32) {
	// 定义包含大小写字母、数字和符号的字符集合
	printf("生成key\n");
	const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
	const size_t max_index = sizeof(charset) - 1;

	std::random_device rd;  // 用于生成随机种子
	std::mt19937 generator(rd());  // 使用 Mersenne Twister 引擎
	std::uniform_int_distribution<> distrib(0, max_index - 1);

	std::string key;
	for (int i = 0; i < length; ++i) {
		key += charset[distrib(generator)];
	}
	return key;
}
int HttpParser(const Buffer& data, Buffer* keyToGenerate) {
	CHttpParser parser;
	size_t size = parser.Parser(data);
	if (size == 0 || (parser.Errno() != 0)) {
		printf("size %llu errno:%u\n", size, parser.Errno());
		return -1;
	}
	if (parser.Method() == HTTP_GET) {
		//get 处理
		UrlParser url("https://47.92.232.122" + parser.Url());
		int ret = url.Parser();
		if (ret != 0) {
			printf("ret = %d url[%s]", ret, "https://47.92.232.122" + parser.Url());
			return -2;
		}
		Buffer uri = url.Uri();
		printf("**** uri = %s", (char*)uri);
		if (uri == "login") {
			std::cout << "处理登录" << std::endl;
			//处理登录
			Buffer time = url["time"];
			Buffer user = url["user"];
			Buffer sign = url["sign"];
			printf("time %s user %s sign %s\n", (char*)time, (char*)user, (char*)sign);
			//数据库的查询
			edoyunLogin_user_mysql dbuser;
			Result result;
			Buffer sql = dbuser.Query("user_name=\"" + user + "\"");
			ret = m_db->Exec(sql, result, dbuser);
			if (ret != 0) {
				printf("sql=%s ret=%d\n", (char*)sql, ret);
				return -3;
			}
			if (result.size() == 0) {
				printf("no result sql=%s ret=%d\n", (char*)sql, ret);
				return -4;
			}
			if (result.size() != 1) {
				printf("more than one sql=%s ret=%d\n", (char*)sql, ret);
				return -5;
			}
			auto user1 = result.front();
			Buffer sign_val = *user1->Fields["sign"]->Value.String;
			//TRACEI("password = %s", (char*)pwd);
			//登录请求的验证
			/*const char* MD5_KEY = "*&^%$#@b.v+h-b*g/h@n!h#n$d^ssx,.kl<kl";
			Buffer md5str = time + MD5_KEY + pwd + salt;
			Buffer md5 = Crypto::MD5(md5str);
			TRACEI("md5 = %s", (char*)md5);*/
			if (sign_val == sign) {
				Buffer expiration_date = *user1->Fields["expiration_date"]->Value.String;
				if (getCurrentTime() < expiration_date) {
					return 0;
				}
				printf("Account expired,expiration date:%s\n", expiration_date);
				return -8;
			}
			printf("账号或密码错误\n");
			return -6;
		}
		else if (uri == "register") {
			//处理注册
			printf("处理注册\n");
			Buffer user = url["user"];
			Buffer sign = url["sign"];
			Buffer key = url["key"];
			printf(" user %s sign %s key %s\n", (char*)user, (char*)sign, (char*)key);
			//数据库的查询
			edoyunLogin_user_mysql dbuser, value;
			Result result;
			Buffer sql = dbuser.Query("user_name=\"" + user + "\"");
			ret = m_db->Exec(sql, result, dbuser);
			if (ret != 0) {
				printf("sql=%s ret=%d\n", (char*)sql, ret);
				return -18;
			}
			if (result.size() != 0) {
				printf("no result sql=%s ret=%d 用户名已存在\n", (char*)sql, ret);
				return -9;
			}
			keyvalue_mysql db_key;
			sql = db_key.Query("KeyValue=\"" + key + "\"");
			ret = m_db->Exec(sql, result, db_key);
			if (ret != 0) {
				printf("sql=%s ret=%d\n", (char*)sql, ret);
				return -10;
			}
			if (result.size() == 0) {
				printf("no result sql=%s ret=%d 无效key\n", (char*)sql, ret);
				return -11;
			}
			value.Fields["user_name"]->LoadFromStr(user);
			value.Fields["user_name"]->Condition = SQL_INSERT;
			value.Fields["sign"]->LoadFromStr(sign);
			value.Fields["sign"]->Condition = SQL_INSERT;
			auto key1 = result.front();
			int dayT = key1->Fields["DayType"]->Value.Integer;
			Buffer expiration_time = addDaysToCurrentDate(dayT);
			std::cout << "expiration:"<<expiration_time << std::endl;
			printf("%s %d expiration:%s\n", __FILE__, __LINE__, expiration_time);
			value.Fields["expiration_date"]->LoadFromStr(expiration_time);
			printf("load expiration time successfully\n");
			value.Fields["expiration_date"]->Condition = SQL_INSERT;
			sql = dbuser.Insert(value);
			ret = m_db->Exec(sql);
			if (ret != 0) {
				printf("sql=%s ret=%d\n", (char*)sql, ret);
				return -12;
			}
			return 0;
		}
		else if (uri == "recharge") {
			//处理充值
			printf("处理充值");
			Buffer user = url["user"];
			Buffer key = url["key"];
			printf(" user %s key %s\n", (char*)user, (char*)key);
			edoyunLogin_user_mysql dbuser, value;
			Result result1, result2;
			Buffer sql = dbuser.Query("user_name=\"" + user + "\"");
			ret = m_db->Exec(sql, result1, dbuser);
			if (ret != 0) {
				printf("sql=%s ret=%d\n", (char*)sql, ret);
				return -13;
			}
			if (result1.size() != 1) {
				printf("no result sql=%s ret=%d 用户名存在数量不为1，为%d\n", (char*)sql, ret, result1.size());
				return -14;
			}
			printf("******%s %s %dresult1.size()=%d\n", __FILE__, __FUNCTION__, __LINE__, result1.size());
			printf("%s %s %d\n", __FILE__, __FUNCTION__, __LINE__);
			auto user1 = result1.front();
			printf("%s %s %d\n", __FILE__, __FUNCTION__, __LINE__);
			Buffer* DateToModify = user1->Fields["expiration_date"]->Value.String;
			printf("%s %s %d\n", __FILE__, __FUNCTION__, __LINE__);
			keyvalue_mysql db_key,value_key;//数据库本身和修改后待插入的key
			printf("%s %s %d\n", __FILE__, __FUNCTION__, __LINE__);
			sql = db_key.Query("KeyValue=\"" + key + "\"");
			printf("%s %s %d\n", __FILE__, __FUNCTION__, __LINE__);
			ret = m_db->Exec(sql, result2, db_key);
			if (ret != 0) {
				printf("sql=%s ret=%d\n", (char*)sql, ret);
				return -15;
			}
			if (result2.size() == 0) {
				printf("no result sql=%s ret=%d 无效key\n", (char*)sql, ret);
				return -16;
			}
			/*value.Fields["user_name"]->LoadFromStr(user);
			value.Fields["user_name"]->Condition = SQL_INSERT;*/
			auto key1 = result2.front();
			int dayT = key1->Fields["DayType"]->Value.Integer;
			Buffer expiration_time = addDaysToDateTime(*DateToModify, dayT);
			value.Fields["user_name"]->LoadFromStr(user);
			value.Fields["user_name"]->Condition = SQL_CONDITION;
			value.Fields["expiration_date"]->LoadFromStr(expiration_time);
			value.Fields["expiration_date"]->Condition = SQL_MODIFY;
			sql = dbuser.Modify(value);
			ret = m_db->Exec(sql);
			if (ret != 0) {
				printf("sql=%s ret=%d\n", (char*)sql, ret);
				return -17;
			}
			value.Fields["KeyValue"]->LoadFromStr(key);
			value.Fields["KeyValue"]->Condition = SQL_CONDITION;
			value.Fields["IsUsed"]->LoadFromStr("1");
			value.Fields["IsUsed"]->Condition=SQL_MODIFY;
			return 0;
		}
		else if (uri == "generateKey") {
			Buffer DayType = url["DayType"];
			printf("DayType %d", DayType);
			keyvalue_mysql db_key, value;
			Result result;
			Buffer KeyValue;
			while (1) {
				KeyValue = generateKey();
				Buffer sql = db_key.Query("KeyValue=\"" + KeyValue + "\"");

				ret = m_db->Exec(sql, result, db_key);
				if (ret != 0) {
					printf("sql=%s ret=%d\n", (char*)sql, ret);
					return -19;
				}
				if (result.size() == 0) break;
			}
			value.Fields["KeyValue"]->LoadFromStr(KeyValue);
			value.Fields["KeyValue"]->Condition = SQL_INSERT;
			value.Fields["DayType"]->LoadFromStr((DayType));
			value.Fields["DayType"]->Condition = SQL_INSERT;
			value.Fields["IsUsed"]->LoadFromStr("0");
			value.Fields["IsUsed"]->Condition = SQL_INSERT;
			Buffer sql = db_key.Insert(value);
			ret = m_db->Exec(sql);
			if (ret != 0) {
				printf("sql=%s ret=%d\n", (char*)sql, ret);
				return -20;
			}
			*keyToGenerate = KeyValue;
			return 0;
		}


	}
	else if (parser.Method() == HTTP_POST) {
		//post 处理
	}
	return -7;
}
Buffer MakeResponse(int ret) {
	Json::Value root;
	root["status"] = ret;
	if (ret != 0) {
		root["message"] = "登录失败，可能是用户名或者密码错误！";
	}
	else {
		root["message"] = "success";
	}
	if (gk != "")root["key"] = gk;
	gk = "";
	Buffer json = root.toStyledString();
	Buffer result = "HTTP/1.1 200 OK\r\n";
	time_t t;
	time(&t);
	tm* ptm = localtime(&t);
	char temp[64] = "";
	strftime(temp, sizeof(temp), "%a, %d %b %G %T GMT\r\n", ptm);
	Buffer Date = Buffer("Date: ") + temp;
	Buffer Server = "Server: Edoyun/1.0\r\nContent-Type: text/html; charset=utf-8\r\nX-Frame-Options: DENY\r\n";
	snprintf(temp, sizeof(temp), "%d", json.size());
	Buffer Length = Buffer("Content-Length: ") + temp + "\r\n";
	Buffer Stub = "X-Content-Type-Options: nosniff\r\nReferrer-Policy: same-origin\r\n\r\n";
	result += Date + Server + Length + Stub + json;
	printf("response: %s", (char*)result);
	return result;
}
void handle_request(int client_socket) {

	char buffer[1024] = { 0 };
	int bytes_received = read(client_socket, buffer, sizeof(buffer) - 1);
	if (bytes_received > 0) {
		std::cout << "接收到数据！\n";
		std::cout << "Received HTTP request:\n" << buffer << std::endl;
		printf("准备执行HttpParser\n");

		//// 简单的 HTTP 响应
		//const char* http_response =
		//    "HTTP/1.1 200 OK\r\n"
		//    "Content-Type: text/plain\r\n"
		//    "Content-Length: 13\r\n"
		//    "\r\n"
		//    "Hello, World!\n";
		int ret = 0;
		printf("开始执行HttpParser\n");
		ret = HttpParser(buffer, &gk);
		printf("HttpParser ret=%d", ret);
		//验证结果的反馈
		if (ret != 0) {//验证失败
			printf("http parser failed!%d", ret);
		}
		Buffer response = MakeResponse(ret);

		send(client_socket, response, strlen(response), 0);
		if (ret != 0) {
			printf("http response failed!%d [%s]", ret, (char*)response);
		}
		else {
			printf("http response success!%d", ret);
		}
	}
	close(client_socket);  // 关闭连接
}

int main() {
	m_db = new CMysqlClient();
	if (m_db == NULL) {
		std::cerr << "no more memory!" << std::endl;
		return -1;
	}
	KeyValue args;
	args["host"] = "127.0.0.1";
	args["user"] = "root";
	args["password"] = "123456";
	args["port"] = 3306;
	args["db"] = "cjn";
	int ret = m_db->Connect(args);
	if (ret < 0)printf("连接到数据库失败");
	const int PORT = 8080;

	// 创建服务器套接字
	int server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd == -1) {
		std::cerr << "Socket creation failed!" << std::endl;
		return -1;
	}

	// 设置地址和端口
	struct sockaddr_in address;
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;  // 监听所有IP
	address.sin_port = htons(PORT);

	// 绑定套接字到地址
	if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
		std::cerr << "Bind failed!" << std::endl;
		return -1;
	}

	// 开始监听
	if (listen(server_fd, 5) < 0) {
		std::cerr << "Listen failed!" << std::endl;
		return -1;
	}

	// 创建线程池
	ThreadPool pool(4);  // 创建线程池，最多4个线程

	std::cout << "Server is running on port " << PORT << "..." << std::endl;

	while (true) {
		// 接受客户端连接
		int client_socket = accept(server_fd, nullptr, nullptr);
		if (client_socket < 0) {
			std::cerr << "Accept failed!" << std::endl;
			continue;
		}

		// 将任务交给线程池处理
		pool.enqueue([client_socket] {
			handle_request(client_socket);
			});
	}

	close(server_fd);  // 关闭服务器套接字
	return 0;
}
