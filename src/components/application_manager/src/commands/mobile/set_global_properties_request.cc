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

#include <algorithm>
#include "application_manager/commands/mobile/set_global_properties_request.h"
#include "application_manager/application_manager_impl.h"
#include "application_manager/application_impl.h"
#include "application_manager/message_helper.h"
#include "interfaces/MOBILE_API.h"
#include "interfaces/HMI_API.h"

namespace application_manager {

namespace commands {

SetGlobalPropertiesRequest::SetGlobalPropertiesRequest(
    const MessageSharedPtr& message)
    : CommandRequestImpl(message),
      is_ui_send_(false),
      is_tts_send_(false),
      is_ui_received_(false),
      is_tts_received_(false),
      ui_result_(hmi_apis::Common_Result::INVALID_ENUM),
      tts_result_(hmi_apis::Common_Result::INVALID_ENUM) {
}

SetGlobalPropertiesRequest::~SetGlobalPropertiesRequest() {
}

void SetGlobalPropertiesRequest::Run() {
  LOG4CXX_INFO(logger_, "SetGlobalPropertiesRequest::Run");

  const smart_objects::SmartObject& msg_params =
      (*message_)[strings::msg_params];

  uint32_t app_id =
      (*message_)[strings::params][strings::connection_key].asUInt();

  ApplicationSharedPtr app = ApplicationManagerImpl::instance()->application(app_id);

  if (!app) {
    LOG4CXX_ERROR_EXT(logger_, "No application associated with session key");
    SendResponse(false, mobile_apis::Result::APPLICATION_NOT_REGISTERED);
    return;
  }

  if (!ValidateConditionalMandatoryParameters(msg_params)) {
    SendResponse(false,
                 mobile_apis::Result::INVALID_DATA,
                 "There are no parameters present in request.");
    return;
  }

  // Check for image file(s) in vrHelpItem
  mobile_apis::Result::eType verification_result =
      MessageHelper::VerifyImageFiles((*message_)[strings::msg_params], app);

  if (mobile_apis::Result::SUCCESS != verification_result) {
    LOG4CXX_ERROR_EXT(
        logger_,
        "MessageHelper::VerifyImageFiles return " << verification_result);
    SendResponse(false, verification_result);
    return;
  }


  bool is_help_prompt_present = msg_params.keyExists(strings::help_prompt);
  bool is_timeout_prompt_present = msg_params.keyExists(
      strings::timeout_prompt);
  bool is_vr_help_title_present = msg_params.keyExists(strings::vr_help_title);
  bool is_vr_help_present = msg_params.keyExists(strings::vr_help);
  bool is_menu_title_present = msg_params.keyExists(hmi_request::menu_title);
  bool is_menu_icon_present = msg_params.keyExists(hmi_request::menu_icon);
  bool is_keyboard_props_present =
      msg_params.keyExists(hmi_request::keyboard_properties);

  // Media-only applications support API v2.1 with less parameters
  if (!app->allowed_support_navigation() &&
      (is_keyboard_props_present ||
       is_menu_icon_present ||
       is_menu_title_present)
       ) {
    const std::string app_type =
        app->is_media_application() ?  "media" : "non-media";

    const std::string message =
        "There are too many parameters for "+app_type+" application.";
    SendResponse(false,
                 mobile_apis::Result::INVALID_DATA,
                 message.c_str());
    return;
  }

  if (is_vr_help_title_present && is_vr_help_present) {
    // check vrhelpitem position index
    if (!CheckVrHelpItemsOrder()) {
      LOG4CXX_ERROR(logger_, "Request rejected");
      SendResponse(false, mobile_apis::Result::REJECTED);
      return;
    }

    app->set_vr_help_title(
        msg_params.getElement(strings::vr_help_title));
    app->set_vr_help(
        msg_params.getElement(strings::vr_help));

    smart_objects::SmartObject params =
        smart_objects::SmartObject(smart_objects::SmartType_Map);

    params[strings::vr_help_title] = (*app->vr_help_title());
    params[strings::vr_help] = (*app->vr_help());
    params[strings::app_id] = app->app_id();
    if (is_menu_title_present) {

      params[hmi_request::menu_title] =
          msg_params[hmi_request::menu_title].asString();
    }
    if (is_menu_icon_present) {

      params[hmi_request::menu_icon] =
          msg_params[hmi_request::menu_icon];
    }
    if (is_keyboard_props_present) {

      params[hmi_request::keyboard_properties] =
          msg_params[hmi_request::keyboard_properties];
    }

    SendHMIRequest(hmi_apis::FunctionID::UI_SetGlobalProperties,
                       &params, true);
  } else if (!is_vr_help_title_present && !is_vr_help_present) {
    const CommandsMap& cmdMap = app->commands_map();
    CommandsMap::const_iterator command_it = cmdMap.begin();

    int32_t index = 0;
    smart_objects::SmartObject vr_help_items;
    for (; cmdMap.end() != command_it; ++command_it) {
      if (false == (*command_it->second).keyExists(strings::vr_commands)) {
        LOG4CXX_ERROR(logger_, "VR synonyms are empty");
        SendResponse(false, mobile_apis::Result::INVALID_DATA);
        return;
      }
      // use only first
      vr_help_items[index][strings::position] = (index + 1);
      vr_help_items[index++][strings::text] =
          (*command_it->second)[strings::vr_commands][0];
    }

    app->set_vr_help_title(smart_objects::SmartObject(app->name()));
    app->set_vr_help(vr_help_items);

    smart_objects::SmartObject params =
        smart_objects::SmartObject(smart_objects::SmartType_Map);

    params[strings::vr_help_title] = (*app->vr_help_title());
    params[strings::vr_help] = (*app->vr_help());
    params[strings::app_id] = app->app_id();
    if (is_menu_title_present) {

      params[hmi_request::menu_title] =
          msg_params[hmi_request::menu_title].asString();
    }
    if (is_menu_icon_present) {

      params[hmi_request::menu_icon] =
          msg_params[hmi_request::menu_icon];
    }
    if (is_keyboard_props_present) {

      params[hmi_request::keyboard_properties] =
          msg_params[hmi_request::keyboard_properties];
    }

    SendHMIRequest(hmi_apis::FunctionID::UI_SetGlobalProperties,
                       &params, true);
  } else {
    LOG4CXX_ERROR(logger_, "Request rejected");
    SendResponse(false, mobile_apis::Result::REJECTED);
    return;
  }

  // check TTS params
  if (is_help_prompt_present || is_timeout_prompt_present) {
    smart_objects::SmartObject params =
        smart_objects::SmartObject(smart_objects::SmartType_Map);

    if (is_help_prompt_present) {
      app->set_help_prompt(
          msg_params.getElement(strings::help_prompt));
      params[strings::help_prompt] = (*app->help_prompt());
    }

    if (is_timeout_prompt_present) {
      app->set_timeout_prompt(
          msg_params.getElement(strings::timeout_prompt));
      params[strings::timeout_prompt] = (*app->timeout_prompt());
    }

    params[strings::app_id] = app->app_id();

    SendHMIRequest(hmi_apis::FunctionID::TTS_SetGlobalProperties,
                   &params, true);
  }
}

bool SetGlobalPropertiesRequest::CheckVrHelpItemsOrder() {
  const smart_objects::SmartObject vr_help = (*message_)[strings::msg_params]
      .getElement(strings::vr_help);

  // vr help item start position must be 1
  const uint32_t vr_help_item_start_position = 1;

  if (vr_help_item_start_position !=
      vr_help.getElement(0).getElement(strings::position).asUInt()) {
    LOG4CXX_ERROR(logger_, "VR help items start position is wrong");
    return false;
  }

  // Check if VR Help Items contains sequential positionss
  size_t i = 0;
  for (size_t j = 1; j < vr_help.length(); ++i, ++j) {
    if ((vr_help.getElement(i).getElement(strings::position).asInt() + 1)
        != vr_help.getElement(j).getElement(strings::position).asInt()) {
      LOG4CXX_ERROR(logger_, "VR help items order is wrong");
      return false;
    }
  }

  return true;
}

void SetGlobalPropertiesRequest::on_event(const event_engine::Event& event) {
  LOG4CXX_INFO(logger_, "SetGlobalPropertiesRequest::on_event");
  const smart_objects::SmartObject& message = event.smart_object();

  ApplicationSharedPtr app = ApplicationManagerImpl::instance()->application(CommandRequestImpl::connection_key());

  switch (event.id()) {
    case hmi_apis::FunctionID::UI_SetGlobalProperties: {
      LOG4CXX_INFO(logger_, "Received UI_SetGlobalProperties event");
      is_ui_received_ = true;
      ui_result_ = static_cast<hmi_apis::Common_Result::eType>(
          message[strings::params][hmi_response::code].asInt());
      break;
    }
    case hmi_apis::FunctionID::TTS_SetGlobalProperties: {
      LOG4CXX_INFO(logger_, "Received TTS_SetGlobalProperties event");
      is_tts_received_ = true;
      tts_result_ = static_cast<hmi_apis::Common_Result::eType>(
          message[strings::params][hmi_response::code].asInt());
      break;
    }
    default: {
      LOG4CXX_ERROR(logger_, "Received unknown event" << event.id());
      return;
    }
  }

  bool result = ((hmi_apis::Common_Result::SUCCESS == ui_result_)
        && (hmi_apis::Common_Result::SUCCESS == tts_result_ ||
            hmi_apis::Common_Result::UNSUPPORTED_RESOURCE == tts_result_))
        || ((hmi_apis::Common_Result::SUCCESS == ui_result_)
            && (hmi_apis::Common_Result::INVALID_ENUM == tts_result_))
        || ((hmi_apis::Common_Result::INVALID_ENUM == ui_result_)
            && (hmi_apis::Common_Result::SUCCESS == tts_result_));

  mobile_apis::Result::eType result_code;
  const char* return_info = NULL;

  if (result) {
    if (hmi_apis::Common_Result::UNSUPPORTED_RESOURCE == tts_result_) {
      result_code = mobile_apis::Result::WARNINGS;
      return_info =
          std::string("Unsupported phoneme type sent in a prompt").c_str();
    } else {
      result_code = static_cast<mobile_apis::Result::eType>(
      std::max(ui_result_, tts_result_));
    }
  } else {
    result_code = static_cast<mobile_apis::Result::eType>(
        std::max(ui_result_, tts_result_));
  }


  SendResponse(result, static_cast<mobile_apis::Result::eType>(result_code),
               return_info, &(message[strings::msg_params]));
  app->UpdateHash();
}

bool SetGlobalPropertiesRequest::IsPendingResponseExist() {

  return is_ui_send_ != is_ui_received_ || is_tts_send_ != is_tts_received_;
}

bool SetGlobalPropertiesRequest::ValidateConditionalMandatoryParameters(
    const smart_objects::SmartObject& params) {
  return params.keyExists(strings::help_prompt)
      || params.keyExists(strings::timeout_prompt)
      || params.keyExists(strings::vr_help_title)
      || params.keyExists(strings::vr_help)
      || params.keyExists(strings::menu_title)
      || params.keyExists(strings::menu_icon)
      || params.keyExists(strings::keyboard_properties);
}

}  // namespace commands

}  // namespace application_manager
