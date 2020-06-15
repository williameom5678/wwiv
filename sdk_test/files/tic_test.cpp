/**************************************************************************/
/*                                                                        */
/*                              WWIV Version 5.x                          */
/*                Copyright (C)2020, WWIV Software Services               */
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
/*                                                                        */
/**************************************************************************/
#include "gtest/gtest.h"

#include "core/datafile.h"
#include "core/file.h"
#include "core_test/file_helper.h"
#include "sdk/files/tic.h"

using namespace wwiv::core;

TEST(TicTest, Smoke) {
  FileHelper helper;

  const std::string kFSXINFO = R"(
Created by HTick, written by Gabriel Plutzar
File fsxinfo.zip
Area fsx_info
Areadesc Weekly Infopacks (fsxNet, etc.)
Desc Weekly infopack for fsxNet
LDesc             ���              ���
LDesc         ���߰�� �ܰ�߰�� ��� ���
LDesc     �   ��� ۰��۰������ܰ�� ۰� net �
LDesc  �����  ��� ��� ���  �۰ ��߲���  �����
LDesc  �  ��  �����   ���  ��� ����۰�  ��� �
LDesc  � ���� ���     �۰� ��� ��� �۰ ���  �
LDesc  � ���� ���     �������  ��� ���  �   �
LDesc  � ��           ���           ��      �
LDesc  � �  fsxnet.txt  -- Info & App       �
LDesc  � �  fsxnet.xxx  -- Nodelist         �
LDesc  �    fsxnet.zaa  -- Nodelist (ZIP)   �
LDesc  � �  history.txt -- History          �
LDesc  �    systems.txt -- BBS in fsxNet    �
LDesc  �    fsxnet.na   -- Echo list        �
LDesc  �    fsx_file.na -- File echo list   �
LDesc  � �                                  �
LDesc  � ��                    ��� infopack �
LDesc  �������������������������ܱ�����������
Replaces fsxinfo.zip
From 21:2/100
To 21:2/115
Origin 21:1/1
Size 12
Crc AF083B2D
Path 21:1/1 1591287303 Thu Jun 04 16:15:03 2020 UTC htick/w32-mvcdll 1.9.0-cur 17-08-15
Seenby 21:1/1
Seenby 21:1/2
Pw WELCOME
Path 21:1/100 1591287395 Thu Jun 04 16:16:35 2020 UTC Mystic/1.12 A46
Seenby 21:1/100
Seenby 21:1/101
Seenby 21:1/151
Path 21:2/100 1591287460 Thu Jun 04 16:17:40 2020 UTC Mystic/1.12 A46
  )";

  const auto file = helper.CreateTempFile("fsxinfo.tic", kFSXINFO);
  const auto arc_path = wwiv::core::FilePath(helper.TempDir(), "fsxinfo.zip");
  {
    File af(arc_path);
    ASSERT_TRUE(af.Open(File::modeBinary | File::modeCreateFile | File::modeReadWrite));
    ASSERT_EQ(12, af.Write("hello world\n"));
    af.Close();
  }

  wwiv::sdk::files::TicParser p(file.parent_path());
  auto o = p.parse("fsxinfo.tic");
  ASSERT_TRUE(o);

  auto t = o.value();
  EXPECT_EQ(t.area, "fsx_info");
  EXPECT_EQ(18u, t.ldesc.size());
  EXPECT_EQ("Weekly infopack for fsxNet", t.desc);
  EXPECT_EQ("fsxinfo.zip", t.file);
  EXPECT_EQ("fsxinfo.zip", t.replaces);
  EXPECT_TRUE(t.exists());
  EXPECT_TRUE(t.crc_valid());
  EXPECT_TRUE(t.size_valid());
  EXPECT_TRUE(t.IsValid());
}
