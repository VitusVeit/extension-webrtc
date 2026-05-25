/**
 * Copyright (c) 2017-2022 Paul-Louis Ageneau
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "configuration.hpp"

#include <cerrno>
#include <cstdlib>

namespace rtc {

static bool parsePort(const string &service, uint16_t *outPort) {
	if (!outPort || service.empty())
		return false;

	char *end = nullptr;
	errno = 0;
	unsigned long value = std::strtoul(service.c_str(), &end, 10);
	if (errno != 0 || end == service.c_str() || *end != '\0' || value > 65535)
		return false;

	*outPort = static_cast<uint16_t>(value);
	return true;
}

IceServer::IceServer(const string &url) : hostname(url), port(0), type(Type::Dummy) {}

IceServer::IceServer(string hostname_, uint16_t port_)
    : hostname(std::move(hostname_)), port(port_), type(Type::Stun) {}

IceServer::IceServer(string hostname_, string service_)
    : hostname(std::move(hostname_)), type(Type::Stun) {
	uint16_t parsedPort = 0;
	if (parsePort(service_, &parsedPort))
		port = parsedPort;
    else
		port = 0;
}

IceServer::IceServer(string hostname_, uint16_t port_, string username_, string password_,
                     RelayType relayType_)
    : hostname(std::move(hostname_)), port(port_), type(Type::Turn), username(std::move(username_)),
      password(std::move(password_)), relayType(relayType_) {}

IceServer::IceServer(string hostname_, string service_, string username_, string password_,
                     RelayType relayType_)
    : hostname(std::move(hostname_)), type(Type::Turn), username(std::move(username_)),
      password(std::move(password_)), relayType(relayType_) {
	uint16_t parsedPort = 0;
	if (parsePort(service_, &parsedPort))
		port = parsedPort;
    else
		port = 0;
}

} // namespace rtc
