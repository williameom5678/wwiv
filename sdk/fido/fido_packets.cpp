/**************************************************************************/
/*                                                                        */
/*                          WWIV Version 5.x                              */
/*               Copyright (C)2016 WWIV Software Services                 */
/*                                                                        */
/*    Licensed  under the  Apache License, Version  2.0 (the "License");  */
/*    you may not use this  file  except in compliance with the License.  */
/*    You may obtain a copy of the License at                             */
/*                                                                        */
/*                http://www.apache.org/licenses/LICENSE-2.0              */
/*                                                                        */
/*    Unless  required  by  applicable  law  or agreed to  in  writing,   */
/*    software  distributed  under  the  License  is  distributed on an   */
/*    "AS IS"  BASIS, WITHOUT  WARRANTIES  OR  CONDITIONS OF ANY  KIND,   */
/*    either  express  or implied.  See  the  License for  the specific   */
/*    language governing permissions and limitations under the License.   */
/**************************************************************************/
#include "sdk/fido/fido_packets.h"

#include <string>

#include "core/file.h"
#include "core/log.h"
#include "core/strings.h"
#include "sdk/datetime.h"
#include "sdk/filenames.h"

using std::string;
using namespace wwiv::core;
using namespace wwiv::strings;

namespace wwiv {
namespace sdk {
namespace fido {

static char ReadCharFromFile(File& f) {
  char buf[1];
  int num_read = f.Read(buf, 1);
  if (num_read == 0) {
    return 0;
  }
  return buf[0];
}

static std::string ReadFixedLengthField(File& f, int len) {
  string s;
  s.resize(len + 1);
  int num_read = f.Read(&s[0], len + 1);
  s.resize(num_read);
  return s;
}

static std::string ReadVariableLengthField(File& f, int max_len) {
  string s;
  for (int i = 0; i < max_len; i++) {
    char ch = ReadCharFromFile(f);
    if (ch == 0) {
      return s;
    } else {
      s.push_back(ch);
    }
  }
  return s;
}

ReadPacketResponse read_packed_message(File& f, FidoPackedMessage& packet) {
  int num_read = f.Read(&packet.nh, sizeof(fido_packed_message_t));
  if (num_read == 0) {
    // at the end of the packet.
    return ReadPacketResponse::END_OF_FILE;
  }
  if (num_read == 2) {
    // FIDO packets have 2 bytes of NULL at the end;
    if (packet.nh.message_type == 0) {
      return ReadPacketResponse::END_OF_FILE;
    }
  }

  if (num_read != sizeof(fido_packed_message_t)) {
    LOG(INFO) << "error reading header, got short read of size: " << num_read
      << "; expected: " << sizeof(net_header_rec);
    return ReadPacketResponse::ERROR;
  }

  if (packet.nh.message_type != 2) {
    LOG(INFO) << "invalid message_type: " << packet.nh.message_type << "; expected: 2";
    //return ReadPacketResponse::ERROR;
  }
  packet.vh.date_time = ReadFixedLengthField(f, 19);
  packet.vh.to_user_name = ReadVariableLengthField(f, 36);
  packet.vh.from_user_name = ReadVariableLengthField(f, 36);
  packet.vh.subject = ReadVariableLengthField(f, 36);
  packet.vh.text = ReadVariableLengthField(f, 256 * 1024);
  return ReadPacketResponse::OK;
}


}
}  // namespace net
}  // namespace wwiv

