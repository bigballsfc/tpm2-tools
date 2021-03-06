//**********************************************************************;
// Copyright (c) 2015, Intel Corporation
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// 3. Neither the name of Intel Corporation nor the names of its contributors
// may be used to endorse or promote products derived from this software without
// specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
// THE POSSIBILITY OF SUCH DAMAGE.
//**********************************************************************;

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

#include <sapi/tpm20.h>

#include "tpm2_options.h"
#include "tpm2_password_util.h"
#include "files.h"
#include "log.h"
#include "tpm2_nv_util.h"
#include "tpm2_tool.h"
#include "tpm2_util.h"

typedef struct tpm_nvdefine_ctx tpm_nvdefine_ctx;
struct tpm_nvdefine_ctx {
    UINT32 nvIndex;
    UINT32 authHandle;
    UINT32 size;
    TPMA_NV nvAttribute;
    TPM2B_AUTH nvAuth;
    TPMS_AUTH_COMMAND session_data;
    char *policy_file;
};

static tpm_nvdefine_ctx ctx = {
    .authHandle = TPM_RH_PLATFORM,
    .nvAttribute = SESSION_ATTRIBUTES_INIT(0),
    .session_data = TPMS_AUTH_COMMAND_INIT(TPM_RS_PW),
    .nvAuth = TPM2B_EMPTY_INIT,
};

static int nv_space_define(TSS2_SYS_CONTEXT *sapi_context) {

    TPM2B_NV_PUBLIC public_info = TPM2B_EMPTY_INIT;

    TPMS_AUTH_RESPONSE session_data_out;
    TSS2_SYS_CMD_AUTHS sessions_data;
    TSS2_SYS_RSP_AUTHS sessions_data_out;

    TPMS_AUTH_COMMAND *session_data_array[1] = {
        &ctx.session_data
    };

    TPMS_AUTH_RESPONSE *session_data_out_array[1] = {
        &session_data_out
    };

    sessions_data_out.rspAuths = &session_data_out_array[0];
    sessions_data.cmdAuths = &session_data_array[0];

    sessions_data_out.rspAuthsCount = 1;
    sessions_data.cmdAuthsCount = 1;

    public_info.t.size = sizeof(TPMI_RH_NV_INDEX) + sizeof(TPMI_ALG_HASH)
            + sizeof(TPMA_NV) + sizeof(UINT16) + sizeof(UINT16);
    public_info.t.nvPublic.nvIndex = ctx.nvIndex;
    public_info.t.nvPublic.nameAlg = TPM_ALG_SHA256;

    // Now set the attributes.
    public_info.t.nvPublic.attributes.val = ctx.nvAttribute.val;

    if (ctx.policy_file) {
        public_info.t.nvPublic.authPolicy.t.size  = BUFFER_SIZE(TPM2B_DIGEST, buffer);
        if(!files_load_bytes_from_path(ctx.policy_file, public_info.t.nvPublic.authPolicy.t.buffer, &public_info.t.nvPublic.authPolicy.t.size )) {
            return false;
        }
    } 

    public_info.t.nvPublic.dataSize = ctx.size;

    TPM_RC rval = Tss2_Sys_NV_DefineSpace(sapi_context, ctx.authHandle,
            &sessions_data, &ctx.nvAuth, &public_info, &sessions_data_out);
    if (rval != TPM_RC_SUCCESS) {
        LOG_ERR("Failed to define NV area at index 0x%x (%d).Error:0x%x",
                ctx.nvIndex, ctx.nvIndex, rval);
        return false;
    }

    LOG_INFO("Success to define NV area at index 0x%x (%d).", ctx.nvIndex, ctx.nvIndex);

    return true;
}

static bool on_option(char key, char *value) {

    bool result;

    switch (key) {
    case 'x':
        result = tpm2_util_string_to_uint32(value, &ctx.nvIndex);
        if (!result) {
            LOG_ERR("Could not convert NV index to number, got: \"%s\"",
                    value);
            return false;
        }

        if (ctx.nvIndex == 0) {
                LOG_ERR("NV Index cannot be 0");
                return false;
        }
        break;
    case 'a':
        result = tpm2_util_string_to_uint32(value, &ctx.authHandle);
        if (!result) {
            LOG_ERR("Could not convert auth handle to number, got: \"%s\"",
                    value);
            return false;
        }

        if (ctx.authHandle == 0) {
            LOG_ERR("Auth handle cannot be 0");
            return false;
        }
        break;
        case 'P':
            result = tpm2_password_util_from_optarg(value, &ctx.session_data.hmac);
            if (!result) {
                LOG_ERR("Invalid handle password, got\"%s\"", value);
                return false;
            }
            break;
    case 's':
        result = tpm2_util_string_to_uint32(value, &ctx.size);
        if (!result) {
            LOG_ERR("Could not convert size to number, got: \"%s\"",
                    value);
            return false;
        }
        break;
    case 't':
        result = tpm2_util_string_to_uint32(value, &ctx.nvAttribute.val);
        if (!result) {
            result = tpm2_nv_util_strtoattr(value, &ctx.nvAttribute);
            if (!result) {
                LOG_ERR("Could not convert NV attribute to number or keyword, got: \"%s\"",
                        value);
                return false;
            }
        }
        break;
    case 'I':
        result = tpm2_password_util_from_optarg(value, &ctx.nvAuth);
        if (!result) {
            LOG_ERR("Invalid index password, got\"%s\"", value);
            return false;
        }
        break;
    case 'L':
        ctx.policy_file = optarg;
        break;
    case 'S':
        if (!tpm2_util_string_to_uint32(optarg, &ctx.session_data.sessionHandle)) {
            LOG_ERR("Could not convert session handle to number, got: \"%s\"",
                    value);
            return false;
        }
        break;
    }

    return true;
}

bool tpm2_tool_onstart(tpm2_options **opts) {

    const struct option topts[] = {
        { "index",                  required_argument,  NULL,   'x' },
        { "auth-handle",             required_argument,  NULL,   'a' },
        { "size",                   required_argument,  NULL,   's' },
        { "attribute",              required_argument,  NULL,   't' },
        { "handle-passwd",           required_argument,  NULL,   'P' },
        { "index-passwd",            required_argument,  NULL,   'I' },
        { "passwdInHex",            no_argument,        NULL,   'X' },
        { "policy-file",            required_argument,  NULL,   'L' },
        { "input-session-handle",   required_argument,  NULL,   'S' },
    };

    *opts = tpm2_options_new("x:a:s:t:P:I:rwdL:S:X", ARRAY_LEN(topts), topts, on_option, NULL);

    return *opts != NULL;
}

int tpm2_tool_onrun(TSS2_SYS_CONTEXT *sapi_context, tpm2_option_flags flags) {

    UNUSED(flags);

    return nv_space_define(sapi_context) != true;
}
