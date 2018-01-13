//
// Copyright 2015 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
#include "Firebase.h"

using std::unique_ptr;

namespace {
std::string makeFirebaseURL(const std::string& path, const std::string& auth) {
  std::string url;
  if (path[0] != '/') {
    url = "/";
  }
  url += path + ".json";
  if (auth.length() > 0) {
    url += "?auth=" + auth;
  }
  return url;
}

}  // namespace

Firebase::Firebase(const std::string& host, const std::string& auth) : host_(host), auth_(auth) {
}

const std::string& Firebase::auth() const {
  return auth_;
}

FirebaseGet Firebase::get(const std::string& path) {
  return FirebaseGet(host_, auth_, path);
}

unique_ptr<FirebaseGet> Firebase::getPtr(const std::string& path) {
  return unique_ptr<FirebaseGet>(new FirebaseGet(host_, auth_, path));
}

FirebaseSet Firebase::set(const std::string& path, const std::string& value) {
  return FirebaseSet(host_, auth_, path, value);
}

unique_ptr<FirebaseSet> Firebase::setPtr(const std::string& path,
                                         const std::string& value) {
  return unique_ptr<FirebaseSet>(
      new FirebaseSet(host_, auth_, path, value));
}

FirebasePush Firebase::push(const std::string& path, const std::string& value) {
  return FirebasePush(host_, auth_, path, value);
}
/*unique_ptr<FirebasePush> Firebase::pushPtr(const std::string& path, const std::string& value) {
  return unique_ptr<FirebasePush>(
      new FirebasePush(host_, auth_, path, value));
}*/

FirebaseRemove Firebase::remove(const std::string& path) {
  return FirebaseRemove(host_, auth_, path);
}

unique_ptr<FirebaseRemove> Firebase::removePtr(const std::string& path) {
  return unique_ptr<FirebaseRemove>(
      new FirebaseRemove(host_, auth_, path));
}

FirebaseStream Firebase::stream(const std::string& path) {
  // TODO: create new client dedicated to stream.
  return FirebaseStream(host_, auth_, path);
}

unique_ptr<FirebaseStream> Firebase::streamPtr(const std::string& path) {
  // TODO: create new client dedicated to stream.
  return unique_ptr<FirebaseStream>(
      new FirebaseStream(host_, auth_, path));
}

// FirebaseCall
FirebaseCall::FirebaseCall(const std::string& host, const std::string& auth,
                           const char* method, const std::string& path,
                           const std::string& data) {
  std::string path_with_auth = makeFirebaseURL(path, auth);

  HTTPClient http_;

  http_.setReuse(false);
  http_.begin(host.c_str(), SERVER_PORT, path_with_auth.c_str(), SERVER_FINGERPRINT);

  bool followRedirect = false;
  if (std::string(method) == "STREAM") {
    method = "GET";
    http_.addHeader("Accept", "text/event-stream");
    followRedirect = true;
  }

  if (followRedirect) {
    const char* headers[] = {"Location"};
    http_.collectHeaders(headers, 1);
  }

  int status = http_.sendRequest(method, (uint8_t*)data.c_str(), data.length());

  // TODO: Add a max redirect check
  if (followRedirect) {
    while (status == HttpStatus::TEMPORARY_REDIRECT) {
      std::string location = http_.header("Location").c_str();
      http_.end();
      http_.begin(location.c_str(), SERVER_FINGERPRINT);
      status = http_.sendRequest("GET", (uint8_t*)location.c_str(), location.length());
    }
  }

  if (status != 200) {
    error_ = FirebaseError(status,
                           std::string(method) + " " + path_with_auth +
                              ": " + HTTPClient::errorToString(status).c_str());
  }

  // if not streaming.
  if (!followRedirect) {
    response_ = http_.getString().c_str();
  }

  http_.end();
}

FirebaseCall::~FirebaseCall() {
}

const JsonObject& FirebaseCall::json() {
  //TODO(edcoyne): This is not efficient, we should do something smarter with
  //the buffers.
  buffer_ = DynamicJsonBuffer();
  return buffer_.parseObject(response().c_str());
}

// FirebaseGet
FirebaseGet::FirebaseGet(const std::string& host, const std::string& auth,
                         const std::string& path,
                         FirebaseHttpClient* http)
  : FirebaseCall(host, auth, "GET", path, "") {
}

// FirebaseSet
FirebaseSet::FirebaseSet(const std::string& host, const std::string& auth,
       const std::string& path, const std::string& value,
       FirebaseHttpClient* http)
  : FirebaseCall(host, auth, "PUT", path, value) {
  if (!error()) {
    // TODO: parse json
    json_ = response();
  }
}

// FirebasePush
FirebasePush::FirebasePush(const std::string& host, const std::string& auth,
                           const std::string& path, const std::string& value)
  : FirebaseCall(host, auth, "POST", path, value) {
  if (!error()) {
    name_ = json()["name"].as<const char*>();
  }
}

// FirebaseRemove
FirebaseRemove::FirebaseRemove(const std::string& host, const std::string& auth,
                               const std::string& path,
                               FirebaseHttpClient* http)
  : FirebaseCall(host, auth, "DELETE", path, "") {
}

// FirebaseStream
FirebaseStream::FirebaseStream(const std::string& host, const std::string& auth,
                               const std::string& path,
                               FirebaseHttpClient* http)
  : FirebaseCall(host, auth, "STREAM", path, "") {
}
