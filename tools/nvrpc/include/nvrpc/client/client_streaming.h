/**
 * SPDX-FileCopyrightText: Copyright (c) 2018-2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once
#include <functional>
#include <memory>

#include <grpcpp/grpcpp.h>

#include "nvrpc/client/base_context.h"
#include "nvrpc/client/executor.h"

#include <glog/logging.h>

namespace nvrpc {
namespace client {

template <typename Request, typename Response>
struct ClientStreaming : public BaseContext
{
  public:
    using PrepareFn = std::function<std::unique_ptr<::grpc::ClientAsyncReaderWriter<Request, Response>>(
        ::grpc::ClientContext*, ::grpc::CompletionQueue*)>;

    using ReadCallback  = std::function<void(Response&&)>;
    using WriteCallback = std::function<void(Request&&)>;

    ClientStreaming(PrepareFn, std::shared_ptr<Executor>, WriteCallback, ReadCallback);
    ~ClientStreaming()
    {
        DVLOG(1) << "ClientStreaming dtor";
    }

    // void Write(Request*);
    bool Write(Request&&);

    std::shared_future<::grpc::Status> Status();
    std::shared_future<::grpc::Status> Done();

    void SetCorked(bool true_or_false)
    {
        m_Corked = true_or_false;
    }

    bool IsCorked() const
    {
        return m_Corked;
    }

    bool ExecutorShouldDeleteContext() const override
    {
        return false;
    }

    void ExecutorShouldDeleteContext(bool true_or_false)
    {
        m_ShouldDelete = true_or_false;
    }

  private:
    bool RunNextState(bool ok) final override
    {
        return (this->*m_NextState)(ok);
    }

    bool RunNextState(bool (ClientStreaming<Request, Response>::*state_fn)(bool), bool ok)
    {
        return (this->*state_fn)(ok);
    }

    class Context : public BaseContext
    {
      public:
        Context(BaseContext* master) : BaseContext(master) {}
        ~Context() override {}

      private:
        bool RunNextState(bool ok) final override
        {
            // DVLOG(1) << "Event for Tag: " << Tag();
            return static_cast<ClientStreaming*>(m_MasterContext)->RunNextState(m_NextState, ok);
        }

        bool (ClientStreaming<Request, Response>::*m_NextState)(bool);

        bool ExecutorShouldDeleteContext() const override
        {
            return false;
        }

        friend class ClientStreaming<Request, Response>;
    };

    ::grpc::Status m_Status;
    ::grpc::ClientContext m_Context;
    std::unique_ptr<::grpc::ClientAsyncReaderWriter<Request, Response>> m_Stream;
    std::promise<::grpc::Status> m_Promise;

    PrepareFn m_PrepareFn;

    ReadCallback m_ReadCallback;
    WriteCallback m_WriteCallback;

    // Context<Request, Response> m_ReadState;
    // Context<Request, Response> m_WriteState;

    Context m_ReadState;
    Context m_WriteState;

    std::mutex m_Mutex;
    std::queue<Response> m_ReadQueue;
    std::queue<Request> m_WriteQueue;

    std::shared_ptr<Executor> m_Executor;

    bool m_Corked;
    bool m_ShouldDelete;

    using ReadHandle     = bool;
    using WriteHandle    = bool;
    using ExecuteHandle  = std::function<void()>;
    using CloseHandle    = bool;
    using FinishHandle   = bool;
    using CompleteHandle = bool;
    using Actions = std::tuple<ReadHandle, WriteHandle, ExecuteHandle, CloseHandle, FinishHandle, CompleteHandle>;

    bool m_Reading, m_Writing, m_Finishing, m_Closing, m_ReadsDone, m_WritesDone, m_FinishDone;

    bool (ClientStreaming<Request, Response>::*m_NextState)(bool);

    Actions EvaluateState();
    void ForwardProgress(Actions& actions);

    bool StateStreamInitialized(bool);
    bool StateReadDone(bool);
    bool StateWriteDone(bool);
    bool StateWritesDoneDone(bool);
    bool StateFinishDone(bool);
    bool StateInvalid(bool);
    bool StateIdle(bool);
};

template <typename Request, typename Response>
ClientStreaming<Request, Response>::ClientStreaming(PrepareFn prepare_fn,
                                                    std::shared_ptr<Executor> executor,
                                                    WriteCallback OnWrite,
                                                    ReadCallback OnRead) :
  m_Executor(executor),
  m_PrepareFn(prepare_fn),
  m_ReadState(this),
  m_WriteState(this),
  m_ReadCallback(OnRead),
  m_WriteCallback(OnWrite),
  m_Reading(false),
  m_Writing(false),
  m_Finishing(false),
  m_Closing(false),
  m_ReadsDone(false),
  m_WritesDone(false),
  m_FinishDone(false),
  m_ShouldDelete(false),
  m_Corked(false)
{
    m_NextState              = &ClientStreaming<Request, Response>::StateStreamInitialized;
    m_ReadState.m_NextState  = &ClientStreaming<Request, Response>::StateInvalid;
    m_WriteState.m_NextState = &ClientStreaming<Request, Response>::StateInvalid;

    m_Stream = m_PrepareFn(&m_Context, m_Executor->GetNextCQ());
    m_Stream->StartCall(this->Tag());
}

/*
template<typename Request, typename Response>
void ClientStreaming<Request, Response>::Write(Request* request)
{
    // TODO: fixes this so it queues up a lambda
    Write(std::move(*request));
}
*/

template <typename Request, typename Response>
bool ClientStreaming<Request, Response>::Write(Request&& request)
{
    Actions actions;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        DVLOG(1) << "Writing Request";

        if (m_WritesDone)
        {
            LOG(WARNING) << "Attempting to Write on a Stream that is closed";
            return false;
        }

        m_WriteQueue.push(std::move(request));
        m_WriteState.m_NextState = &ClientStreaming<Request, Response>::StateWriteDone;

        actions = EvaluateState();
    }
    ForwardProgress(actions);
    return true;
}

template <typename Request, typename Response>
std::shared_future<::grpc::Status> ClientStreaming<Request, Response>::Done()
{
    Actions actions;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        DVLOG(1) << "Sending WritesDone - Closing Client -> Server side of the stream";

        m_WritesDone = true;

        actions = EvaluateState();
    }
    ForwardProgress(actions);
    return m_Promise.get_future();
}

template <typename Request, typename Response>
std::shared_future<::grpc::Status> ClientStreaming<Request, Response>::Status()
{
    return m_Promise.get_future();
}

template <typename Request, typename Response>
typename ClientStreaming<Request, Response>::Actions ClientStreaming<Request, Response>::EvaluateState()
{
    ReadHandle should_read         = false;
    WriteHandle should_write       = false;
    ExecuteHandle should_execute   = nullptr;
    CloseHandle should_close       = false;
    FinishHandle should_finish     = false;
    CompleteHandle should_complete = false;

    if (m_NextState == &ClientStreaming<Request, Response>::StateStreamInitialized)
    {
        DVLOG(1) << "Action Queued: Stream Initializing";
    }
    else
    {
        if (!m_Reading && !m_ReadsDone)
        {
            should_read = true;
            m_Reading   = true;
            m_ReadQueue.emplace();
            m_ReadState.m_NextState = &ClientStreaming<Request, Response>::StateReadDone;

            should_execute = [this, response = std::move(m_ReadQueue.front())]() mutable {
                m_ReadCallback(std::move(response));
            };
            m_ReadQueue.pop();
        }
        if (!m_Writing && !m_WriteQueue.empty())
        {
            should_write             = true;
            m_Writing                = true;
            m_WriteState.m_NextState = &ClientStreaming<Request, Response>::StateWriteDone;
        }
        if (!m_Closing && !m_Writing && m_WritesDone)
        {
            should_close             = true;
            m_Closing                = true;
            m_WriteState.m_NextState = &ClientStreaming<Request, Response>::StateWritesDoneDone;
        }
        if (!m_Reading && !m_Writing && !m_Finishing && m_ReadsDone && m_WritesDone && !m_FinishDone)
        {
            should_finish = true;
            m_Finishing   = true;
            m_NextState   = &ClientStreaming<Request, Response>::StateFinishDone;
        }
        if (m_ReadsDone && m_WritesDone && m_FinishDone)
        {
            should_complete = true;
        }
    }

    // clang-format off
    DVLOG(1) << (should_read ? 1 : 0) << (should_write ? 1 : 0) << (should_execute ? 1 : 0)
               << (should_finish ? 1 : 0)
               << " -- " << m_Reading << m_Writing << m_Finishing
               << " -- " << m_ReadsDone << m_WritesDone
               << " -- " << m_Finishing;
    // clang-format on

    return std::make_tuple(should_read, should_write, should_execute, should_close, should_finish, should_complete);
}

template <class Request, class Response>
void ClientStreaming<Request, Response>::ForwardProgress(Actions& actions)
{
    ReadHandle should_read         = std::get<0>(actions);
    WriteHandle should_write       = std::get<1>(actions);
    ExecuteHandle should_execute   = std::get<2>(actions);
    CloseHandle should_close       = std::get<3>(actions);
    FinishHandle should_finish     = std::get<4>(actions);
    CompleteHandle should_complete = std::get<5>(actions);

    if (should_read)
    {
        DVLOG(1) << "Posting Read/Recv";
        m_Stream->Read(&m_ReadQueue.back(), m_ReadState.Tag());
    }
    if (should_write)
    {
        DVLOG(1) << "Writing/Sending Request";
        if (m_Corked)
        {
            ::grpc::WriteOptions options;
            options.set_corked();
            m_Stream->Write(m_WriteQueue.front(), options, m_WriteState.Tag());
        }
        else
        {
            m_Stream->Write(m_WriteQueue.front(), m_WriteState.Tag());
        }
    }
    if (should_close)
    {
        DVLOG(1) << "Sending WritesDone to Server";
        m_Stream->WritesDone(m_WriteState.Tag());
    }
    if (should_execute)
    {
        DVLOG(1) << "Kicking off Execution of Received Request";
        should_execute();
    }
    if (should_finish)
    {
        DVLOG(1) << "Closing Stream - Finish";
        m_Stream->Finish(&m_Status, Tag());
    }
    if (should_complete)
    {
        DVLOG(1) << "Completing Promise";
        m_Promise.set_value(std::move(m_Status));
    }
}

template <typename Request, typename Response>
bool ClientStreaming<Request, Response>::StateStreamInitialized(bool ok)
{
    if (!ok)
    {
        DVLOG(1) << "Stream Failed to Initialize";
        return false;
    }

    Actions actions;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        DVLOG(1) << "StreamInitialized";

        m_NextState = &ClientStreaming<Request, Response>::StateInvalid;

        m_Reading = true;
        m_ReadQueue.emplace();
        m_ReadState.m_NextState = &ClientStreaming<Request, Response>::StateReadDone;

        actions = EvaluateState();
    }
    DVLOG(1) << "Posting Initial Read/Recv";
    m_Stream->Read(&m_ReadQueue.back(), m_ReadState.Tag());
    ForwardProgress(actions);
    return true;
}

template <typename Request, typename Response>
bool ClientStreaming<Request, Response>::StateReadDone(bool ok)
{
    Actions actions;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        DVLOG(1) << "ReadDone: " << (ok ? "OK" : "NOT OK");

        m_Reading               = false;
        m_ReadState.m_NextState = &ClientStreaming<Request, Response>::StateInvalid;

        if (!ok)
        {
            DVLOG(1) << "Server is closing the read/download portion of the stream";
            m_ReadsDone  = true;
            m_WritesDone = true;
            m_Closing    = true;
            if (m_Writing)
            {
                m_Context.TryCancel();
            }
        }

        actions = EvaluateState();
    }
    ForwardProgress(actions);
    return true;
}

template <typename Request, typename Response>
bool ClientStreaming<Request, Response>::StateWriteDone(bool ok)
{
    Actions actions;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        DVLOG(1) << "WriteDone: " << (ok ? "OK" : "NOT OK");

        m_Writing = false;
        m_WriteQueue.pop();
        m_WriteState.m_NextState = &ClientStreaming<Request, Response>::StateInvalid;

        if (!ok)
        {
            // Invalidate any outstanding reads on stream
            DLOG(ERROR) << "Failed to Write to Stream - shutting down";
            m_WritesDone = true;
            if (!m_ReadsDone)
            {
                m_Context.TryCancel();
            }
            return false;
        }

        actions = EvaluateState();
    }
    ForwardProgress(actions);
    return true;
}

template <typename Request, typename Response>
bool ClientStreaming<Request, Response>::StateWritesDoneDone(bool ok)
{
    Actions actions;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        DVLOG(1) << "WritesDoneDone: " << (ok ? "OK" : "NOT OK");

        // m_Closing = false;  // keep m_Closing true
        m_WriteState.m_NextState = &ClientStreaming<Request, Response>::StateInvalid;

        if (!ok)
        {
            LOG(ERROR) << "Failed to close write/upload portion of stream";
            if (!m_ReadsDone)
            {
                m_Context.TryCancel();
            }
            return true;
        }

        actions = EvaluateState();
    }
    ForwardProgress(actions);
    return true;
}

template <typename Request, typename Response>
bool ClientStreaming<Request, Response>::StateFinishDone(bool ok)
{
    Actions actions;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        DVLOG(1) << "FinishedDone: " << (ok ? "OK" : "NOT OK");

        m_Finishing  = false;
        m_FinishDone = true;

        if (!ok)
        {
            LOG(ERROR) << "Failed to close read/download portion of the stream";
            m_Context.TryCancel();
            return false;
        }

        actions = EvaluateState();
    }
    ForwardProgress(actions);
    return true;
}
/*
        DVLOG(1) << "Read/Download portion of the stream has closed";

        std::lock_guard<std::mutex> lock(m_Mutex);

        if(m_WriteState.m_NextState == &ClientStreaming<Request, Response>::StateInvalid)
        {
            DVLOG(1) << "Write/Upload has already finished - completing future";

            m_ReadState.m_NextState = &ClientStreaming<Request, Response>::StateInvalid;
        }
        else
        {
            DVLOG(1) << "Received Finished from Server before Client has sent Done writing";
            m_Context.TryCancel();
        }
        return true;
*/

template <typename Request, typename Response>
bool ClientStreaming<Request, Response>::StateInvalid(bool ok)
{
    LOG(FATAL) << "Your logic is bad - you should never have come here";
    return false;
}

}  // namespace client
}  // namespace nvrpc
