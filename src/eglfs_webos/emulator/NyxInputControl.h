// Copyright (c) 2015-2020 LG Electronics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#ifndef _NYXINPUTCONTROL_H_
#define _NYXINPUTCONTROL_H_

#include "InputControl.h"

class NyxInputControl: public InputControl {
public:
    NyxInputControl(nyx_device_type_t type, nyx_device_id_t id);
    virtual ~NyxInputControl();

    virtual bool on();
    virtual bool off();
    virtual bool setRate(int rate);

    virtual nyx_device_handle_t getHandle() { return m_handle; }

private:
    nyx_device_handle_t m_handle;
};

#endif /* _NYXINPUTCONTROL_H_ */
