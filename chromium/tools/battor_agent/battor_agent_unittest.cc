// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/battor_agent/battor_agent.h"

#include "base/test/test_simple_task_runner.h"
#include "base/thread_task_runner_handle.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "tools/battor_agent/battor_protocol_types.h"

using namespace testing;

using std::vector;

namespace battor {

namespace {

BattOrControlMessageAck kInitAck{BATTOR_CONTROL_MESSAGE_TYPE_INIT, 0};
BattOrControlMessageAck kSetGainAck{BATTOR_CONTROL_MESSAGE_TYPE_SET_GAIN, 0};
BattOrControlMessageAck kStartTracingAck{
    BATTOR_CONTROL_MESSAGE_TYPE_START_SAMPLING_SD, 0};

// Creates a byte vector copy of the specified object.
template <typename T>
scoped_ptr<std::vector<char>> ToCharVector(const T& object) {
  return scoped_ptr<std::vector<char>>(new std::vector<char>(
      reinterpret_cast<const char*>(&object),
      reinterpret_cast<const char*>(&object) + sizeof(T)));
}

MATCHER_P2(
    BufferEq,
    expected_buffer,
    expected_buffer_size,
    "Makes sure that the argument has the same contents as the buffer.") {
  return memcmp(reinterpret_cast<const void*>(arg),
                reinterpret_cast<const void*>(expected_buffer),
                expected_buffer_size) == 0;
}

scoped_ptr<vector<char>> CreateFrame(const BattOrFrameHeader& frame_header,
                                     const RawBattOrSample* samples,
                                     const size_t& num_samples) {
  scoped_ptr<vector<char>> bytes(new vector<char>(
      sizeof(BattOrFrameHeader) + sizeof(RawBattOrSample) * num_samples));
  memcpy(bytes->data(), &frame_header, sizeof(BattOrFrameHeader));
  memcpy(bytes->data() + sizeof(BattOrFrameHeader), samples,
         sizeof(RawBattOrSample) * num_samples);

  return bytes;
}

class MockBattOrConnection : public BattOrConnection {
 public:
  MockBattOrConnection(BattOrConnection::Listener* listener)
      : BattOrConnection(listener) {}
  ~MockBattOrConnection() override {}

  MOCK_METHOD0(Open, void());
  MOCK_METHOD0(Close, void());
  MOCK_METHOD3(SendBytes,
               void(BattOrMessageType type,
                    const void* buffer,
                    size_t bytes_to_send));
  MOCK_METHOD1(ReadMessage, void(BattOrMessageType type));
  MOCK_METHOD0(Flush, void());

 private:
  DISALLOW_COPY_AND_ASSIGN(MockBattOrConnection);
};

}  // namespace

// TestableBattOrAgent uses a fake BattOrConnection to be testable.
class TestableBattOrAgent : public BattOrAgent {
 public:
  TestableBattOrAgent(BattOrAgent::Listener* listener)
      : BattOrAgent("/dev/test", listener, nullptr, nullptr) {
    connection_ = scoped_ptr<BattOrConnection>(new MockBattOrConnection(this));
  }

  MockBattOrConnection* GetConnection() {
    return static_cast<MockBattOrConnection*>(connection_.get());
  }
};

// BattOrAgentTest provides a BattOrAgent and captures the results of its
// tracing commands.
class BattOrAgentTest : public testing::Test, public BattOrAgent::Listener {
 public:
  BattOrAgentTest()
      : task_runner_(new base::TestSimpleTaskRunner()),
        thread_task_runner_handle_(task_runner_) {}

  void OnStartTracingComplete(BattOrError error) override {
    is_command_complete_ = true;
    command_error_ = error;
  }

  void OnStopTracingComplete(const std::string& trace,
                             BattOrError error) override {
    is_command_complete_ = true;
    command_error_ = error;
    trace_ = trace;
  }

 protected:
  void SetUp() override {
    agent_.reset(new TestableBattOrAgent(this));
    task_runner_->ClearPendingTasks();
    is_command_complete_ = false;
    command_error_ = BATTOR_ERROR_NONE;
  }

  // Possible states that the BattOrAgent can be in.
  enum class BattOrAgentState {
    // States required to connect to a BattOr.
    CONNECTED,

    // States required to StartTracing.
    RESET_SENT,
    INIT_SENT,
    INIT_ACKED,
    SET_GAIN_SENT,
    GAIN_ACKED,
    START_TRACING_SENT,
    START_TRACING_COMPLETE,

    // States required to StopTracing.
    EEPROM_REQUEST_SENT,
    EEPROM_RECEIVED,
    SAMPLES_REQUEST_SENT,
    CALIBRATION_FRAME_SENT,
  };

  // Runs BattOrAgent::StartTracing until it reaches the specified state by
  // feeding it the callbacks it needs to progress.
  void RunStartTracingTo(BattOrAgentState end_state) {
    is_command_complete_ = false;

    GetAgent()->StartTracing();
    GetTaskRunner()->RunUntilIdle();

    GetAgent()->OnConnectionOpened(true);
    GetTaskRunner()->RunUntilIdle();

    if (end_state == BattOrAgentState::CONNECTED)
      return;

    GetAgent()->OnBytesSent(true);
    GetTaskRunner()->RunUntilIdle();

    if (end_state == BattOrAgentState::RESET_SENT)
      return;

    GetAgent()->OnBytesSent(true);
    GetTaskRunner()->RunUntilIdle();

    if (end_state == BattOrAgentState::INIT_SENT)
      return;

    GetAgent()->OnMessageRead(true, BATTOR_MESSAGE_TYPE_CONTROL_ACK,
                              ToCharVector(kInitAck));
    GetTaskRunner()->RunUntilIdle();

    if (end_state == BattOrAgentState::INIT_ACKED)
      return;

    GetAgent()->OnBytesSent(true);
    GetTaskRunner()->RunUntilIdle();

    if (end_state == BattOrAgentState::SET_GAIN_SENT)
      return;

    GetAgent()->OnMessageRead(true, BATTOR_MESSAGE_TYPE_CONTROL_ACK,
                              ToCharVector(kSetGainAck));
    GetTaskRunner()->RunUntilIdle();

    if (end_state == BattOrAgentState::GAIN_ACKED)
      return;

    GetAgent()->OnBytesSent(true);
    GetTaskRunner()->RunUntilIdle();

    if (end_state == BattOrAgentState::START_TRACING_SENT)
      return;

    // Make sure that we're actually forwarding to a state in the start tracing
    // state machine.
    DCHECK(end_state == BattOrAgentState::START_TRACING_COMPLETE);

    GetAgent()->OnMessageRead(true, BATTOR_MESSAGE_TYPE_CONTROL_ACK,
                              ToCharVector(kStartTracingAck));
    GetTaskRunner()->RunUntilIdle();
  }

  // Runs BattOrAgent::StopTracing until it reaches the specified state by
  // feeding it the callbacks it needs to progress.
  void RunStopTracingTo(BattOrAgentState end_state) {
    is_command_complete_ = false;

    GetAgent()->StopTracing();
    GetTaskRunner()->RunUntilIdle();

    GetAgent()->OnConnectionOpened(true);
    GetTaskRunner()->RunUntilIdle();

    if (end_state == BattOrAgentState::CONNECTED)
      return;

    GetAgent()->OnBytesSent(true);
    GetTaskRunner()->RunUntilIdle();

    if (end_state == BattOrAgentState::EEPROM_REQUEST_SENT)
      return;

    BattOrEEPROM eeprom;
    eeprom.r1 = 1;
    eeprom.r2 = 1;
    eeprom.r3 = 1;
    eeprom.low_gain = 1;
    eeprom.low_gain_correction_offset = 0;
    eeprom.low_gain_correction_factor = 1;
    eeprom.sd_sample_rate = 1000;

    GetAgent()->OnMessageRead(true, BATTOR_MESSAGE_TYPE_CONTROL_ACK,
                              ToCharVector(eeprom));
    GetTaskRunner()->RunUntilIdle();

    if (end_state == BattOrAgentState::EEPROM_RECEIVED)
      return;

    GetAgent()->OnBytesSent(true);
    GetTaskRunner()->RunUntilIdle();

    if (end_state == BattOrAgentState::SAMPLES_REQUEST_SENT)
      return;

    DCHECK(end_state == BattOrAgentState::CALIBRATION_FRAME_SENT);

    BattOrFrameHeader cal_frame_header{0, sizeof(RawBattOrSample)};
    RawBattOrSample cal_frame[] = {RawBattOrSample{1, 1}};
    GetAgent()->OnMessageRead(true, BATTOR_MESSAGE_TYPE_SAMPLES,
                              CreateFrame(cal_frame_header, cal_frame, 1));
    GetTaskRunner()->RunUntilIdle();
  }

  TestableBattOrAgent* GetAgent() { return agent_.get(); }

  scoped_refptr<base::TestSimpleTaskRunner> GetTaskRunner() {
    return task_runner_;
  }

  bool IsCommandComplete() { return is_command_complete_; }
  BattOrError GetCommandError() { return command_error_; }
  std::string GetTrace() { return trace_; }

 private:
  scoped_refptr<base::TestSimpleTaskRunner> task_runner_;
  // Needed to support ThreadTaskRunnerHandle::Get() in code under test.
  base::ThreadTaskRunnerHandle thread_task_runner_handle_;

  scoped_ptr<TestableBattOrAgent> agent_;
  bool is_command_complete_;
  BattOrError command_error_;
  std::string trace_;
};

TEST_F(BattOrAgentTest, StartTracing) {
  testing::InSequence s;
  EXPECT_CALL(*GetAgent()->GetConnection(), Open());

  BattOrControlMessage reset_msg{BATTOR_CONTROL_MESSAGE_TYPE_RESET, 0, 0};
  EXPECT_CALL(
      *GetAgent()->GetConnection(),
      SendBytes(BATTOR_MESSAGE_TYPE_CONTROL,
                BufferEq(&reset_msg, sizeof(reset_msg)), sizeof(reset_msg)));

  EXPECT_CALL(*GetAgent()->GetConnection(), Flush());
  BattOrControlMessage init_msg{BATTOR_CONTROL_MESSAGE_TYPE_INIT, 0, 0};
  EXPECT_CALL(
      *GetAgent()->GetConnection(),
      SendBytes(BATTOR_MESSAGE_TYPE_CONTROL,
                BufferEq(&init_msg, sizeof(init_msg)), sizeof(init_msg)));

  EXPECT_CALL(*GetAgent()->GetConnection(),
              ReadMessage(BATTOR_MESSAGE_TYPE_CONTROL_ACK));

  BattOrControlMessage set_gain_msg{BATTOR_CONTROL_MESSAGE_TYPE_SET_GAIN,
                                    BATTOR_GAIN_LOW, 0};
  EXPECT_CALL(*GetAgent()->GetConnection(),
              SendBytes(BATTOR_MESSAGE_TYPE_CONTROL,
                        BufferEq(&set_gain_msg, sizeof(set_gain_msg)),
                        sizeof(set_gain_msg)));

  EXPECT_CALL(*GetAgent()->GetConnection(),
              ReadMessage(BATTOR_MESSAGE_TYPE_CONTROL_ACK));

  BattOrControlMessage start_tracing_msg{
      BATTOR_CONTROL_MESSAGE_TYPE_START_SAMPLING_SD, 0, 0};
  EXPECT_CALL(*GetAgent()->GetConnection(),
              SendBytes(BATTOR_MESSAGE_TYPE_CONTROL,
                        BufferEq(&start_tracing_msg, sizeof(start_tracing_msg)),
                        sizeof(start_tracing_msg)));

  EXPECT_CALL(*GetAgent()->GetConnection(),
              ReadMessage(BATTOR_MESSAGE_TYPE_CONTROL_ACK));

  RunStartTracingTo(BattOrAgentState::START_TRACING_COMPLETE);
  EXPECT_TRUE(IsCommandComplete());
  EXPECT_EQ(BATTOR_ERROR_NONE, GetCommandError());
}

TEST_F(BattOrAgentTest, StartTracingFailsWithoutConnection) {
  GetAgent()->StartTracing();
  GetTaskRunner()->RunUntilIdle();

  GetAgent()->OnConnectionOpened(false);
  GetTaskRunner()->RunUntilIdle();

  EXPECT_TRUE(IsCommandComplete());
  EXPECT_EQ(BATTOR_ERROR_CONNECTION_FAILED, GetCommandError());
}

TEST_F(BattOrAgentTest, StartTracingFailsIfResetSendFails) {
  RunStartTracingTo(BattOrAgentState::CONNECTED);
  GetAgent()->OnBytesSent(false);
  GetTaskRunner()->RunUntilIdle();

  EXPECT_TRUE(IsCommandComplete());
  EXPECT_EQ(BATTOR_ERROR_SEND_ERROR, GetCommandError());
}

TEST_F(BattOrAgentTest, StartTracingFailsIfInitSendFails) {
  RunStartTracingTo(BattOrAgentState::RESET_SENT);
  GetAgent()->OnBytesSent(false);
  GetTaskRunner()->RunUntilIdle();

  EXPECT_TRUE(IsCommandComplete());
  EXPECT_EQ(BATTOR_ERROR_SEND_ERROR, GetCommandError());
}

TEST_F(BattOrAgentTest, StartTracingFailsIfInitAckReadFails) {
  RunStartTracingTo(BattOrAgentState::INIT_SENT);
  GetAgent()->OnMessageRead(false, BATTOR_MESSAGE_TYPE_CONTROL_ACK, nullptr);
  GetTaskRunner()->RunUntilIdle();

  EXPECT_TRUE(IsCommandComplete());
  EXPECT_EQ(BATTOR_ERROR_RECEIVE_ERROR, GetCommandError());
}

TEST_F(BattOrAgentTest, StartTracingFailsIfInitWrongAckRead) {
  RunStartTracingTo(BattOrAgentState::INIT_SENT);
  GetAgent()->OnMessageRead(true, BATTOR_MESSAGE_TYPE_CONTROL_ACK,
                            ToCharVector(kStartTracingAck));
  GetTaskRunner()->RunUntilIdle();

  EXPECT_TRUE(IsCommandComplete());
  EXPECT_EQ(BATTOR_ERROR_UNEXPECTED_MESSAGE, GetCommandError());
}

TEST_F(BattOrAgentTest, StartTracingFailsIfSetGainSendFails) {
  RunStartTracingTo(BattOrAgentState::RESET_SENT);
  GetAgent()->OnBytesSent(false);
  GetTaskRunner()->RunUntilIdle();

  EXPECT_TRUE(IsCommandComplete());
  EXPECT_EQ(BATTOR_ERROR_SEND_ERROR, GetCommandError());
}

TEST_F(BattOrAgentTest, StartTracingFailsIfSetGainAckReadFails) {
  RunStartTracingTo(BattOrAgentState::SET_GAIN_SENT);
  GetAgent()->OnMessageRead(false, BATTOR_MESSAGE_TYPE_CONTROL_ACK, nullptr);
  GetTaskRunner()->RunUntilIdle();

  EXPECT_TRUE(IsCommandComplete());
  EXPECT_EQ(BATTOR_ERROR_RECEIVE_ERROR, GetCommandError());
}

TEST_F(BattOrAgentTest, StartTracingFailsIfSetGainWrongAckRead) {
  RunStartTracingTo(BattOrAgentState::SET_GAIN_SENT);
  GetAgent()->OnMessageRead(true, BATTOR_MESSAGE_TYPE_CONTROL_ACK,
                            ToCharVector(kStartTracingAck));
  GetTaskRunner()->RunUntilIdle();

  EXPECT_TRUE(IsCommandComplete());
  EXPECT_EQ(BATTOR_ERROR_UNEXPECTED_MESSAGE, GetCommandError());
}

TEST_F(BattOrAgentTest, StartTracingFailsIfStartTracingSendFails) {
  RunStartTracingTo(BattOrAgentState::RESET_SENT);
  GetAgent()->OnBytesSent(false);
  GetTaskRunner()->RunUntilIdle();

  EXPECT_TRUE(IsCommandComplete());
  EXPECT_EQ(BATTOR_ERROR_SEND_ERROR, GetCommandError());
}

TEST_F(BattOrAgentTest, StartTracingFailsIfStartTracingAckReadFails) {
  RunStartTracingTo(BattOrAgentState::START_TRACING_SENT);
  GetAgent()->OnMessageRead(false, BATTOR_MESSAGE_TYPE_CONTROL_ACK, nullptr);
  GetTaskRunner()->RunUntilIdle();

  EXPECT_TRUE(IsCommandComplete());
  EXPECT_EQ(BATTOR_ERROR_RECEIVE_ERROR, GetCommandError());
}

TEST_F(BattOrAgentTest, StartTracingFailsIfStartTracingWrongAckRead) {
  RunStartTracingTo(BattOrAgentState::START_TRACING_SENT);
  GetAgent()->OnMessageRead(true, BATTOR_MESSAGE_TYPE_CONTROL_ACK,
                            ToCharVector(kInitAck));
  GetTaskRunner()->RunUntilIdle();

  EXPECT_TRUE(IsCommandComplete());
  EXPECT_EQ(BATTOR_ERROR_UNEXPECTED_MESSAGE, GetCommandError());
}

TEST_F(BattOrAgentTest, StopTracing) {
  testing::InSequence s;
  EXPECT_CALL(*GetAgent()->GetConnection(), Open());

  BattOrControlMessage request_eeprom_msg{
      BATTOR_CONTROL_MESSAGE_TYPE_READ_EEPROM, sizeof(BattOrEEPROM), 0};
  EXPECT_CALL(
      *GetAgent()->GetConnection(),
      SendBytes(BATTOR_MESSAGE_TYPE_CONTROL,
                BufferEq(&request_eeprom_msg, sizeof(request_eeprom_msg)),
                sizeof(request_eeprom_msg)));

  EXPECT_CALL(*GetAgent()->GetConnection(),
              ReadMessage(BATTOR_MESSAGE_TYPE_CONTROL_ACK));

  BattOrControlMessage request_samples_msg{
      BATTOR_CONTROL_MESSAGE_TYPE_READ_SD_UART, 0, 0};
  EXPECT_CALL(
      *GetAgent()->GetConnection(),
      SendBytes(BATTOR_MESSAGE_TYPE_CONTROL,
                BufferEq(&request_samples_msg, sizeof(request_samples_msg)),
                sizeof(request_samples_msg)));

  // We send the agent four frames: a calibration frame, and two real frames,
  // and one zero-length frame to indicate that we're done.
  EXPECT_CALL(*GetAgent()->GetConnection(),
              ReadMessage(BATTOR_MESSAGE_TYPE_SAMPLES))
      .Times(4);

  RunStopTracingTo(BattOrAgentState::SAMPLES_REQUEST_SENT);

  // Send the calibration frame.
  BattOrFrameHeader cal_frame_header{0, 2 * sizeof(RawBattOrSample)};
  RawBattOrSample cal_frame[] = {
      RawBattOrSample{1, 1}, RawBattOrSample{2, 2},
  };
  GetAgent()->OnMessageRead(true, BATTOR_MESSAGE_TYPE_SAMPLES,
                            CreateFrame(cal_frame_header, cal_frame, 2));

  // Send the two real data frames.
  BattOrFrameHeader frame_header1{0, 3 * sizeof(RawBattOrSample)};
  RawBattOrSample frame1[] = {
      RawBattOrSample{1, 1}, RawBattOrSample{2, 2}, RawBattOrSample{3, 3},
  };
  GetAgent()->OnMessageRead(true, BATTOR_MESSAGE_TYPE_SAMPLES,
                            CreateFrame(frame_header1, frame1, 3));

  BattOrFrameHeader frame_header2{0, 1 * sizeof(RawBattOrSample)};
  RawBattOrSample frame2[] = {RawBattOrSample{1, 1}};
  GetAgent()->OnMessageRead(true, BATTOR_MESSAGE_TYPE_SAMPLES,
                            CreateFrame(frame_header2, frame2, 1));

  // Send an empty last frame to indicate that we're done.
  BattOrFrameHeader frame_header3{0, 0 * sizeof(RawBattOrSample)};
  RawBattOrSample frame3[] = {};
  GetAgent()->OnMessageRead(true, BATTOR_MESSAGE_TYPE_SAMPLES,
                            CreateFrame(frame_header3, frame3, 0));

  EXPECT_TRUE(IsCommandComplete());
  EXPECT_EQ(BATTOR_ERROR_NONE, GetCommandError());
  EXPECT_EQ("0.00 -0.3 -0.6\n1.00 0.3 0.6\n2.00 0.9 1.8\n3.00 -0.3 -0.6\n",
            GetTrace());
}

TEST_F(BattOrAgentTest, StopTracingFailsWithoutConnection) {
  GetAgent()->StopTracing();
  GetTaskRunner()->RunUntilIdle();

  GetAgent()->OnConnectionOpened(false);
  GetTaskRunner()->RunUntilIdle();

  EXPECT_TRUE(IsCommandComplete());
  EXPECT_EQ(BATTOR_ERROR_CONNECTION_FAILED, GetCommandError());
}

TEST_F(BattOrAgentTest, StopTracingFailsIfEEPROMRequestSendFails) {
  RunStopTracingTo(BattOrAgentState::CONNECTED);
  GetAgent()->OnBytesSent(false);
  GetTaskRunner()->RunUntilIdle();

  EXPECT_TRUE(IsCommandComplete());
  EXPECT_EQ(BATTOR_ERROR_SEND_ERROR, GetCommandError());
}

TEST_F(BattOrAgentTest, StopTracingRetriesEEPROMRead) {
  RunStopTracingTo(BattOrAgentState::EEPROM_REQUEST_SENT);

  GetAgent()->OnMessageRead(false, BATTOR_MESSAGE_TYPE_CONTROL_ACK, nullptr);
  GetTaskRunner()->RunUntilIdle();

  GetAgent()->OnMessageRead(true, BATTOR_MESSAGE_TYPE_CONTROL_ACK,
                            ToCharVector(BattOrEEPROM()));
  GetTaskRunner()->RunUntilIdle();

  // Give confirmation that the samples request was sent.
  GetAgent()->OnBytesSent(true);
  GetTaskRunner()->RunUntilIdle();

  BattOrFrameHeader cal_frame_header{0, sizeof(RawBattOrSample)};
  RawBattOrSample cal_frame[] = {RawBattOrSample{1, 1}};
  GetAgent()->OnMessageRead(true, BATTOR_MESSAGE_TYPE_SAMPLES,
                            CreateFrame(cal_frame_header, cal_frame, 1));
  GetTaskRunner()->RunUntilIdle();

  BattOrFrameHeader frame_header{0, 0};
  RawBattOrSample frame[] = {};
  GetAgent()->OnMessageRead(true, BATTOR_MESSAGE_TYPE_SAMPLES,
                            CreateFrame(frame_header, frame, 0));
  GetTaskRunner()->RunUntilIdle();

  EXPECT_TRUE(IsCommandComplete());
  EXPECT_EQ(BATTOR_ERROR_NONE, GetCommandError());
}

TEST_F(BattOrAgentTest, StopTracingFailsIfEEPROMReadFails) {
  RunStopTracingTo(BattOrAgentState::EEPROM_REQUEST_SENT);

  for (int i = 0; i < 20; i++) {
    GetAgent()->OnMessageRead(false, BATTOR_MESSAGE_TYPE_CONTROL_ACK, nullptr);
    GetTaskRunner()->RunUntilIdle();
  }

  EXPECT_TRUE(IsCommandComplete());
  EXPECT_EQ(BATTOR_ERROR_RECEIVE_ERROR, GetCommandError());
}

TEST_F(BattOrAgentTest, StopTracingFailsIfEEPROMWrongAckRead) {
  RunStopTracingTo(BattOrAgentState::EEPROM_REQUEST_SENT);
  GetAgent()->OnMessageRead(true, BATTOR_MESSAGE_TYPE_CONTROL_ACK,
                            ToCharVector(kInitAck));
  GetTaskRunner()->RunUntilIdle();

  EXPECT_TRUE(IsCommandComplete());
  EXPECT_EQ(BATTOR_ERROR_UNEXPECTED_MESSAGE, GetCommandError());
}

TEST_F(BattOrAgentTest, StopTracingFailsIfRequestSamplesFails) {
  RunStopTracingTo(BattOrAgentState::EEPROM_RECEIVED);
  GetAgent()->OnBytesSent(false);
  GetTaskRunner()->RunUntilIdle();

  EXPECT_TRUE(IsCommandComplete());
  EXPECT_EQ(BATTOR_ERROR_SEND_ERROR, GetCommandError());
}

TEST_F(BattOrAgentTest, StopTracingSucceedsWithOneCalibrationFrameReadFailure) {
  RunStopTracingTo(BattOrAgentState::SAMPLES_REQUEST_SENT);

  // Make a read fail in order to make sure that the agent will retry.
  GetAgent()->OnMessageRead(false, BATTOR_MESSAGE_TYPE_SAMPLES, nullptr);
  GetTaskRunner()->RunUntilIdle();

  BattOrFrameHeader cal_frame_header{0, sizeof(RawBattOrSample)};
  RawBattOrSample cal_frame[] = {RawBattOrSample{1, 1}};
  GetAgent()->OnMessageRead(true, BATTOR_MESSAGE_TYPE_SAMPLES,
                            CreateFrame(cal_frame_header, cal_frame, 1));
  GetTaskRunner()->RunUntilIdle();

  BattOrFrameHeader frame_header{0, 0};
  RawBattOrSample frame[] = {};
  GetAgent()->OnMessageRead(true, BATTOR_MESSAGE_TYPE_SAMPLES,
                            CreateFrame(frame_header, frame, 0));
  GetTaskRunner()->RunUntilIdle();

  EXPECT_TRUE(IsCommandComplete());
  EXPECT_EQ(BATTOR_ERROR_NONE, GetCommandError());
}

TEST_F(BattOrAgentTest, StopTracingFailsWithManyCalibrationFrameReadFailures) {
  RunStopTracingTo(BattOrAgentState::SAMPLES_REQUEST_SENT);

  // We attempt the read a max of 20 times: send that many failures.
  for (int i = 0; i < 20; i++) {
    GetAgent()->OnMessageRead(false, BATTOR_MESSAGE_TYPE_SAMPLES, nullptr);
    GetTaskRunner()->RunUntilIdle();
  }

  EXPECT_TRUE(IsCommandComplete());
  EXPECT_EQ(BATTOR_ERROR_RECEIVE_ERROR, GetCommandError());
}

TEST_F(BattOrAgentTest, StopTracingSucceedsWithOneDataFrameReadFailure) {
  RunStopTracingTo(BattOrAgentState::CALIBRATION_FRAME_SENT);

  // Make a read fail in order to make sure that the agent will retry.
  GetAgent()->OnMessageRead(false, BATTOR_MESSAGE_TYPE_SAMPLES, nullptr);
  GetTaskRunner()->RunUntilIdle();

  BattOrFrameHeader frame_header{0, 0};
  RawBattOrSample frame[] = {};
  GetAgent()->OnMessageRead(true, BATTOR_MESSAGE_TYPE_SAMPLES,
                            CreateFrame(frame_header, frame, 0));
  GetTaskRunner()->RunUntilIdle();

  EXPECT_TRUE(IsCommandComplete());
  EXPECT_EQ(BATTOR_ERROR_NONE, GetCommandError());
}

TEST_F(BattOrAgentTest, StopTracingFailsWithManyDataFrameReadFailures) {
  RunStopTracingTo(BattOrAgentState::CALIBRATION_FRAME_SENT);

  // We attempt the read a max of 20 times: send that many failures.
  for (int i = 0; i < 20; i++) {
    GetAgent()->OnMessageRead(false, BATTOR_MESSAGE_TYPE_SAMPLES, nullptr);
    GetTaskRunner()->RunUntilIdle();
  }

  EXPECT_TRUE(IsCommandComplete());
  EXPECT_EQ(BATTOR_ERROR_RECEIVE_ERROR, GetCommandError());
}

TEST_F(BattOrAgentTest, StopTracingRetriesResetEachFrame) {
  RunStopTracingTo(BattOrAgentState::CALIBRATION_FRAME_SENT);

  // Send 11 failures on two different reads: because the retry count should
  // reset after a successful read, this should still be okay.
  for (int i = 0; i < 11; i++) {
    GetAgent()->OnMessageRead(false, BATTOR_MESSAGE_TYPE_SAMPLES, nullptr);
    GetTaskRunner()->RunUntilIdle();
  }

  BattOrFrameHeader frame_header1{0, 1 * sizeof(RawBattOrSample)};
  RawBattOrSample frame1[] = {RawBattOrSample{1, 1}};
  GetAgent()->OnMessageRead(true, BATTOR_MESSAGE_TYPE_SAMPLES,
                            CreateFrame(frame_header1, frame1, 1));
  GetTaskRunner()->RunUntilIdle();

  for (int i = 0; i < 11; i++) {
    GetAgent()->OnMessageRead(false, BATTOR_MESSAGE_TYPE_SAMPLES, nullptr);
    GetTaskRunner()->RunUntilIdle();
  }

  BattOrFrameHeader frame_header2{0, 0};
  RawBattOrSample frame2[] = {};
  GetAgent()->OnMessageRead(true, BATTOR_MESSAGE_TYPE_SAMPLES,
                            CreateFrame(frame_header2, frame2, 0));
  GetTaskRunner()->RunUntilIdle();

  EXPECT_TRUE(IsCommandComplete());
  EXPECT_EQ(BATTOR_ERROR_NONE, GetCommandError());
}

TEST_F(BattOrAgentTest, StopTracingFailsIfSamplesReadHasWrongType) {
  RunStopTracingTo(BattOrAgentState::SAMPLES_REQUEST_SENT);
  GetAgent()->OnMessageRead(true, BATTOR_MESSAGE_TYPE_CONTROL_ACK,
                            ToCharVector(kInitAck));
  GetTaskRunner()->RunUntilIdle();

  EXPECT_TRUE(IsCommandComplete());
  EXPECT_EQ(BATTOR_ERROR_UNEXPECTED_MESSAGE, GetCommandError());
}

TEST_F(BattOrAgentTest, StopTracingFailsIfCalibrationFrameHasWrongLength) {
  RunStopTracingTo(BattOrAgentState::SAMPLES_REQUEST_SENT);

  // Send a calibration frame with a mismatch between the frame length in the
  // header and the actual frame length.
  BattOrFrameHeader cal_frame_header{0, 1 * sizeof(RawBattOrSample)};
  RawBattOrSample cal_frame[] = {
      RawBattOrSample{1, 1}, RawBattOrSample{2, 2},
  };
  GetAgent()->OnMessageRead(true, BATTOR_MESSAGE_TYPE_SAMPLES,
                            CreateFrame(cal_frame_header, cal_frame, 2));

  EXPECT_TRUE(IsCommandComplete());
  EXPECT_EQ(BATTOR_ERROR_UNEXPECTED_MESSAGE, GetCommandError());
}

TEST_F(BattOrAgentTest, StopTracingFailsIfDataFrameHasWrongLength) {
  RunStopTracingTo(BattOrAgentState::SAMPLES_REQUEST_SENT);

  BattOrFrameHeader cal_frame_header{0, 1 * sizeof(RawBattOrSample)};
  RawBattOrSample cal_frame[] = {
      RawBattOrSample{1, 1},
  };
  GetAgent()->OnMessageRead(true, BATTOR_MESSAGE_TYPE_SAMPLES,
                            CreateFrame(cal_frame_header, cal_frame, 1));

  // Send a data frame with a mismatch between the frame length in the
  // header and the actual frame length.
  BattOrFrameHeader frame_header{0, 2 * sizeof(RawBattOrSample)};
  RawBattOrSample frame[] = {RawBattOrSample{1, 1}};
  GetAgent()->OnMessageRead(true, BATTOR_MESSAGE_TYPE_SAMPLES,
                            CreateFrame(frame_header, frame, 1));
  GetTaskRunner()->RunUntilIdle();

  EXPECT_TRUE(IsCommandComplete());
  EXPECT_EQ(BATTOR_ERROR_UNEXPECTED_MESSAGE, GetCommandError());
}

TEST_F(BattOrAgentTest, StopTracingFailsIfCalibrationFrameMissingByte) {
  RunStopTracingTo(BattOrAgentState::SAMPLES_REQUEST_SENT);

  BattOrFrameHeader cal_frame_header{0, 2 * sizeof(RawBattOrSample)};
  RawBattOrSample cal_frame[] = {
      RawBattOrSample{1, 1}, RawBattOrSample{2, 2},
  };

  // Remove the last byte from the frame to make it invalid.
  scoped_ptr<vector<char>> cal_frame_bytes =
      CreateFrame(cal_frame_header, cal_frame, 2);
  cal_frame_bytes->pop_back();

  GetAgent()->OnMessageRead(true, BATTOR_MESSAGE_TYPE_SAMPLES,
                            std::move(cal_frame_bytes));

  EXPECT_TRUE(IsCommandComplete());
  EXPECT_EQ(BATTOR_ERROR_UNEXPECTED_MESSAGE, GetCommandError());
}

TEST_F(BattOrAgentTest, StopTracingFailsIfDataFrameMissingByte) {
  RunStopTracingTo(BattOrAgentState::SAMPLES_REQUEST_SENT);

  BattOrFrameHeader cal_frame_header{0, 1 * sizeof(RawBattOrSample)};
  RawBattOrSample cal_frame[] = {
      RawBattOrSample{1, 1},
  };
  GetAgent()->OnMessageRead(true, BATTOR_MESSAGE_TYPE_SAMPLES,
                            CreateFrame(cal_frame_header, cal_frame, 1));

  BattOrFrameHeader frame_header{0, 1};
  RawBattOrSample frame[] = {RawBattOrSample{1, 1}};

  // Remove the last byte from the frame to make it invalid.
  scoped_ptr<vector<char>> frame_bytes = CreateFrame(frame_header, frame, 2);
  frame_bytes->pop_back();

  GetAgent()->OnMessageRead(true, BATTOR_MESSAGE_TYPE_SAMPLES,
                            std::move(frame_bytes));
  GetTaskRunner()->RunUntilIdle();

  EXPECT_TRUE(IsCommandComplete());
  EXPECT_EQ(BATTOR_ERROR_UNEXPECTED_MESSAGE, GetCommandError());
}

}  // namespace battor
