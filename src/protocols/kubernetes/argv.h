/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */


#ifndef GUAC_KUBERNETES_ARGV_H
#define GUAC_KUBERNETES_ARGV_H

#include "config.h"

#include <guacamole/user.h>

/**
 * The maximum number of bytes to allow for any argument value received via an
 * argv stream, including null terminator.
 */
#define GUAC_KUBERNETES_ARGV_MAX_LENGTH 16384

/**
 * Handles an incoming stream from a Guacamole "argv" instruction, updating the
 * given connection parameter if that parameter is allowed to be updated.
 */
guac_user_argv_handler guac_kubernetes_argv_handler;

#endif
