/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2018-2020 Couchbase, Inc.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */

#include <string>
#include <iostream>
#include <cstdlib>
#include <cstring>

#include <libcouchbase/couchbase.h>

#include "cJSON.h"

static void fail(lcb_STATUS err, const char *msg)
{
    std::cerr << "[ERROR] " << msg << ": " << lcb_strerror_short(err) << "\n";
    exit(EXIT_FAILURE);
}

static void check(lcb_STATUS err, const char *msg)
{
    if (err != LCB_SUCCESS) {
        fail(err, msg);
    }
}

static void row_callback(lcb_INSTANCE *instance, int, const lcb_RESPANALYTICS *resp)
{
    // tag::result[]
    int *idx;
    const char *row;
    size_t nrow;
    lcb_STATUS rc = lcb_respanalytics_status(resp);

    lcb_respanalytics_cookie(resp, reinterpret_cast<void **>(&idx));
    lcb_respanalytics_row(resp, &row, &nrow);
    if (rc != LCB_SUCCESS) {
        const lcb_RESPHTTP *http;
        std::cout << lcb_strerror_short(rc);
        lcb_respanalytics_http_response(resp, &http);
        // end::result[]
        if (http) {
            uint16_t status;
            lcb_resphttp_http_status(http, &status);
            std::cout << ", HTTP status: " << status;
        }
        printf("\n");
        if (nrow) {
            cJSON *json;
            char *data = new char[nrow + 1];
            memcpy(data, row, nrow); /* copy to ensure trailing zero */
            json = cJSON_Parse(data);
            if (json && json->type == cJSON_Object) {
                cJSON *errors = cJSON_GetObjectItem(json, "errors");
                if (errors && errors->type == cJSON_Array) {
                    int ii, nerrors = cJSON_GetArraySize(errors);
                    for (ii = 0; ii < nerrors; ii++) {
                        cJSON *err = cJSON_GetArrayItem(errors, ii);
                        if (err && err->type == cJSON_Object) {
                            cJSON *code, *msg;
                            code = cJSON_GetObjectItem(err, "code");
                            msg = cJSON_GetObjectItem(err, "msg");
                            if (code && code->type == cJSON_Number && msg && msg->type == cJSON_String) {
                                printf(
                                    "\x1b[1mcode\x1b[0m: \x1b[31m%d\x1b[0m, \x1b[1mmessage\x1b[0m: \x1b[31m%s\x1b[0m\n",
                                    code->valueint, msg->valuestring);
                            }
                        }
                    }
                }
            }
            delete[] data;
        }
    }

    if (lcb_respanalytics_is_final(resp)) {
        std::cout << "META: ";
        printf("\x1b[1mMETA:\x1b[0m ");
    } else {
        std::cout << (*idx)++ << " ";
    }
    std::cout << std::string(row, nrow) << "\n";
    if (lcb_respanalytics_is_final(resp)) {
        std::cout << "\n";
    }

    lcb_DEFERRED_HANDLE *handle = nullptr;
    lcb_respanalytics_deferred_handle_extract(resp, &handle);
    if (handle) {
        const char *status;
        size_t status_len;
        lcb_deferred_handle_status(handle, &status, &status_len);
        std::cout << "DEFERRED: " << std::string(status, status_len) << "\n";
        lcb_deferred_handle_callback(handle, row_callback);
        check(lcb_deferred_handle_poll(instance, idx, handle), "poll deferred query status");
        lcb_deferred_handle_destroy(handle);
    }
}

int main(int argc, char *argv[])
{
    lcb_INSTANCE *instance;

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " couchbase://host/beer-sample [ password [ username ] ]\n";
        exit(EXIT_FAILURE);
    }

    {
        lcb_CREATEOPTS *create_options = nullptr;
        lcb_createopts_create(&create_options, LCB_TYPE_BUCKET);
        lcb_createopts_connstr(create_options, argv[1], strlen(argv[1]));
        if (argc > 3) {
            lcb_createopts_credentials(create_options, argv[3], strlen(argv[3]), argv[2], strlen(argv[2]));
        }
        check(lcb_create(&instance, create_options), "create couchbase handle");
        lcb_createopts_destroy(create_options);
        check(lcb_connect(instance), "schedule connection");
        lcb_wait(instance, LCB_WAIT_DEFAULT);
        check(lcb_get_bootstrap_status(instance), "bootstrap from cluster");
        {
            char *bucket = nullptr;
            check(lcb_cntl(instance, LCB_CNTL_GET, LCB_CNTL_BUCKETNAME, &bucket), "get bucket name");
            if (strcmp(bucket, "beer-sample") != 0) {
                fail(LCB_ERR_INVALID_ARGUMENT, "expected bucket to be \"beer-sample\"");
            }
        }
    }

    {
        // tag::analytics[]
        const char *stmt = "SELECT * FROM breweries LIMIT 2";
        lcb_CMDANALYTICS *cmd;
        int idx = 0;
        lcb_cmdanalytics_create(&cmd);
        lcb_cmdanalytics_callback(cmd, row_callback);
        lcb_cmdanalytics_statement(cmd, stmt, strlen(stmt));
        lcb_cmdanalytics_deferred(cmd, 1);
        check(lcb_analytics(instance, &idx, cmd), "schedule analytics query");
        std::cout << "----> " << stmt << "\n";
        lcb_cmdanalytics_destroy(cmd);
        lcb_wait(instance, LCB_WAIT_DEFAULT);
        // end::analytics[]
    }
    /* Now that we're all done, close down the connection handle */
    lcb_destroy(instance);
    return 0;
}
