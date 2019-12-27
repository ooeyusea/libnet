#include "libhttp.h"
#include "util.h"
#include <algorithm>

namespace libhttp {
	std::unordered_map<std::string, std::string> ParseHttpRequestCookie(const std::string& value) {
		std::unordered_map<std::string, std::string> ret;
		NetBuffer buf((char*)value.c_str(), (int32_t)value.size(), nullptr, 0);

		int32_t lineStart = 0;
		while (true) {
			int32_t lineEnd = buf.Find(lineStart, ';');
			if (lineEnd == -1)
				lineEnd = buf.Size();

			int32_t sep = buf.Find(lineStart, lineEnd, '=');
			int32_t keyStart = buf.FindIf(lineStart, sep, [](char c) { return !isspace(c); });

			if (keyStart == -1) {
				if (lineEnd == buf.Size())
					break;

				lineStart = lineEnd + 1;
				continue;
			}

			int32_t keyEnd = buf.RFindIf(keyStart, sep, [](char c) { return !isspace(c); });
			if (keyEnd == -1)
				keyEnd = sep;
			else
				++keyEnd;

			if (keyStart < keyEnd) {
				std::string key = buf.ReadBlock(keyStart, keyEnd);

				int32_t valueStart = buf.FindIf(sep + 1, lineEnd, [](char c) { return !isspace(c); });
				if (valueStart == -1)
					ret[key] = "";
				else {
					int32_t valueEnd = buf.RFindIf(valueStart, lineEnd, [](char c) { return !isspace(c); });
					if (valueEnd == -1)
						valueEnd = lineEnd;
					else
						++valueEnd;

					ret[key] = buf.ReadBlock(valueStart, valueEnd);
				}
			}

			if (lineEnd == buf.Size())
				break;

			lineStart = lineEnd + 1;
		}

		return ret;
	}

	int32_t ParseHttpRequest(const NetBuffer& buf, int8_t& status, HttpRequest& req) {
		int32_t offset = 0;
		if (status < PS_METHOD_PARSED) {
			int32_t end = buf.Find(offset, ' ');
			if (end == -1)
				return offset;

			req.method = buf.ReadBlock(offset, end);
			offset = end + 1;

			status = PS_METHOD_PARSED;
		}

		if (status < PS_URI_PARSED) {
			int32_t end = buf.Find(offset, ' ');
			if (end == -1)
				return offset;

			req.uri = buf.ReadBlock(offset, end);
			offset = end + 1;

			status = PS_URI_PARSED;
		}

		if (status < PS_PROTO_PARSED) {
			int32_t end = buf.Find(offset, "\r\n");
			if (end == -1)
				return offset;

			req.proto = buf.ReadBlock(offset, end);
			offset = end + 2;

			status = PS_PROTO_PARSED;
		}

		if (status < PS_DATA_PARSED) {
			while (true) {
				int32_t end = buf.Find(offset, "\r\n");
				if (end == -1)
					return offset;
				else if (end == offset) {
					status = PS_DATA_PARSED;
					offset += 2;
					break;
				}

				int32_t sep = buf.Find(offset, end, ':');
				if (sep == -1)
					return -1;

				int32_t keyStart = buf.FindIf(offset, sep, [](char c) { return !isspace(c); });

				if (keyStart == -1)
					return -1;

				int32_t keyEnd = buf.RFindIf(keyStart, sep, [](char c) { return !isspace(c); });
				if (keyEnd == -1)
					keyEnd = sep;
				else
					++keyEnd;

				std::string key = buf.ReadBlock(keyStart, keyEnd);
				std::transform(key.begin(), key.end(), key.begin(), ::tolower);

				int32_t valueStart = buf.FindIf(sep + 1, end, [](char c) { return !isspace(c); });

				std::string value;
				if (valueStart != -1) {
					int32_t valueEnd = buf.RFindIf(valueStart, end, [](char c) { return !isspace(c); });
					if (valueEnd == -1)
						valueEnd = end;
					else
						++valueEnd;

					value = buf.ReadBlock(valueStart, valueEnd);
				}
				
				if (key == "connection") {
					std::transform(value.begin(), value.end(), value.begin(), ::tolower);
					req.keepAlive = (value == "keep-alive");
				}
				else if (key == "host") {
					req.host = std::move(value);
				}
				else if (key == "user-agent") {
					req.userAgent = std::move(value);
				}
				else if (key == "cookie") {
					req.cookies = ParseHttpRequestCookie(value);
				}
				else {
					if (key == "content-length")
						req.contentSize = atoi(value.c_str());

					req.other[key] = value;
				}

				offset = end + 2;
			}
		}

		if (req.contentSize > 0) {
			if (buf.Size() < offset + req.contentSize)
				return offset;

			req.queryContent = buf.ReadBlock(offset, offset + req.contentSize);
			status = PS_COMPLETE;
			offset += req.contentSize;
		}
		else
			status = PS_COMPLETE;

		return offset;
	}

	std::string BuildHttpResponse(const std::string& proto, int32_t error, const std::string& contentType) {
		char msg[1024];
		if (error == 200) {
			snprintf(msg, 1024, "%s 200 OK\r\nContent-Type: %s\r\nDate: %s\r\nTransfer-Encoding: chunked\r\n\r\n", proto.c_str(), contentType.c_str());
		}
		else {

		}

		return msg;
	}
}