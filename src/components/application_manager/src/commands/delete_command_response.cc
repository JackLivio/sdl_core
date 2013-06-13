/*

 Copyright (c) 2013, Ford Motor Company
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:

 Redistributions of source code must retain the above copyright notice, this
 list of conditions and the following disclaimer.

 Redistributions in binary form must reproduce the above copyright notice,
 this list of conditions and the following
 disclaimer in the documentation and/or other materials provided with the
 distribution.

 Neither the name of the Ford Motor Company nor the names of its contributors
 may be used to endorse or promote products derived from this software
 without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 POSSIBILITY OF SUCH DAMAGE.
 */

#include "application_manager/commands/delete_command_response.h"
#include "application_manager/application_manager_impl.h"
#include "application_manager/application_impl.h"
#include "application_manager/message_chaining.h"

namespace application_manager {

namespace commands {

DeleteCommandResponse::DeleteCommandResponse(
    const MessageSharedPtr& message): CommandResponseImpl(message) {
}

DeleteCommandResponse::~DeleteCommandResponse() {
}

void DeleteCommandResponse::Run() {
  if ((*message_)[strings::params][strings::success] == false)
  {
    SendResponse();
    return;
  }

  namespace smart_objects = NsSmartDeviceLink::NsSmartObjects;

  // TODO(DK) HMI Request Id
  const int function_id =
      (*message_)[strings::params][strings::function_id].asInt();

  const int correlation_id =
      (*message_)[strings::params][strings::correlation_id].asInt();

  // TODO(DK): HMI code Id
  const NsSmartDeviceLinkRPC::V2::Result::eType code =
      static_cast<NsSmartDeviceLinkRPC::V2::Result::eType>(
      (*message_)[strings::msg_params][hmi_response::code].asInt());

  // TODO(VS) HMI Request Id
  const int ui_cmd_id = 202;
  const int vr_cmd_id = 203;

  MessageChaining* msg_chain =
  ApplicationManagerImpl::instance()->GetMessageChain(correlation_id);

  if (NULL == msg_chain) {
    return;
  }

  smart_objects::CSmartObject data = msg_chain->data();

  if (function_id == ui_cmd_id) {
    msg_chain->set_ui_response_result(code);
  } else if (function_id == vr_cmd_id) {
    msg_chain->set_vr_response_result(code);
  }

  // we need to retrieve stored response code before message chain decrase
  const bool result_ui = msg_chain->ui_response_result();
  const bool result_vr = msg_chain->vr_response_result();

  // sending response
  if (ApplicationManagerImpl::instance()->DecreaseMessageChain(
      correlation_id)) {
    ApplicationImpl* app = static_cast<ApplicationImpl*>(
          ApplicationManagerImpl::instance()->
          application(data[strings::params][strings::app_id]));

    smart_objects::CSmartObject* command =
        app->FindCommand(
            data[strings::msg_params][strings::cmd_id].asInt());

    if (command)  {
      if (true == result_ui) {
        (*command).erase(strings::menu_params);
      }

      if (true == result_vr) {
        (*command).erase(strings::vr_commands);
      }

      if (!(*command).keyExists(strings::menu_params) &&
          !(*command).keyExists(strings::vr_commands)) {
        app->RemoveCommand(
            data[strings::msg_params][strings::cmd_id].asInt());
        (*message_)[strings::msg_params][strings::success] = true;
        (*message_)[strings::msg_params][strings::result_code] =
            NsSmartDeviceLinkRPC::V2::Result::SUCCESS;
        SendResponse();
      }
    }
  }
}
}  // namespace commands

}  // namespace application_manager
