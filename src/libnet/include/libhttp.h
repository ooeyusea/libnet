#ifndef __LIBHTTP_H__
#define __LIBHTTP_H__
#include "libnet.h"
#include <string>
#include <unordered_map>

namespace libhttp {
	using NetBuffer = libnet::NetBuffer;

	struct HttpRequest {
		std::string method;
		std::string uri;
		std::string proto;
		std::string host;
		std::string userAgent;
		std::string queryContent;
		std::unordered_map<std::string, std::string> cookies;
		std::unordered_map<std::string, std::string> other;
		bool keepAlive = false;
		int32_t contentSize = -1;

		inline void Clear() {
			method.clear();
			uri.clear();
			proto.clear();
			host.clear();
			userAgent.clear();
			queryContent.clear();
			cookies.clear();
			other.clear();
			keepAlive = false;
			contentSize = -1;
		}
	};

	int32_t ParseHttpRequest(const NetBuffer& buf, int8_t& status, HttpRequest& req);
	std::string BuildHttpResponse(const std::string& proto, int32_t error, const std::string& contentType);

	enum {
		PS_NONE,
		PS_METHOD_PARSED,
		PS_URI_PARSED,
		PS_PROTO_PARSED,
		PS_DATA_PARSED,
		PS_COMPLETE,
	};
}

#endif //__LIBHTTP_H__
