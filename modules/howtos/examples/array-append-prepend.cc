/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2015-2020 Couchbase, Inc.
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
#include <cstring>

#include <libcouchbase/couchbase.h>

static void check(lcb_STATUS err)
{
    if (err != LCB_SUCCESS) {
        std::cerr << "ERROR: " << lcb_strerror_short(err) << "\n";
        exit(EXIT_FAILURE);
    }
}

static void get_callback(lcb_INSTANCE *, int cbtype, const lcb_RESPGET *resp)
{
    std::cerr << "Got callback for " << lcb_strcbtype(cbtype) << "\n";

    lcb_STATUS rc = lcb_respget_status(resp);
    if (rc != LCB_SUCCESS) {
        std::cerr << "Operation failed: " << lcb_strerror_short(rc) << "\n";
        return;
    }

    const char *value;
    size_t nvalue;
    lcb_respget_value(resp, &value, &nvalue);
    std::cerr << "Value: " << std::string(value, nvalue) << "\n";
}

static void store_callback(lcb_INSTANCE *, int cbtype, const lcb_RESPSTORE *resp)
{
    std::cerr << "Got callback for " << lcb_strcbtype(cbtype) << "\n";

    lcb_STATUS rc = lcb_respstore_status(resp);
    if (rc != LCB_SUCCESS) {
        std::cerr << "Operation failed: " << lcb_strerror_short(rc) << "\n";
        return;
    }

    std::cerr << "OK\n";
}

static void subdoc_callback(lcb_INSTANCE *, int cbtype, const lcb_RESPSUBDOC *resp)
{
    lcb_STATUS rc = lcb_respsubdoc_status(resp);

    std::cerr << "Got callback for " << lcb_strcbtype(cbtype) << "\n";
    if (rc != LCB_SUCCESS) {
        std::cerr << "Operation failed: " << lcb_strerror_short(rc) << "\n";
        return;
    }

    if (lcb_respsubdoc_result_size(resp) > 0) {
        const char *value;
        size_t nvalue;
        lcb_respsubdoc_result_value(resp, 0, &value, &nvalue);
        rc = lcb_respsubdoc_result_status(resp, 0);
        std::cerr << "Status: " << lcb_strerror_short(rc) << ". Value: " << std::string(value, nvalue) << "\n";
    } else {
        std::cerr << "No result!\n";
    }
}

// Function to issue an lcb_get() (and print the state of the document)
static void demoKey(lcb_INSTANCE *instance, const char *key)
{
    std::cout << "Retrieving '" << key << "'\n";
    std::cout << "====\n";
    lcb_CMDGET *gcmd;
    lcb_cmdget_create(&gcmd);
    lcb_cmdget_key(gcmd, key, strlen(key));
    lcb_STATUS rc = lcb_get(instance, nullptr, gcmd);
    if (rc != LCB_SUCCESS) {
        std::cerr << "Failed to schedule GET operation: " << lcb_strerror_short(rc) << "\n";
        return;
    }
    lcb_cmdget_destroy(gcmd);
    lcb_wait(instance, LCB_WAIT_DEFAULT);
    std::cout << "====\n\n";
}

// cluster_run mode
#define DEFAULT_CONNSTR "couchbase://localhost"
int main(int argc, char **argv)
{
    lcb_CREATEOPTS *crst = nullptr;
    const char *connstr, *username, *password;

    if (argc > 1) {
        connstr = argv[1];
    } else {
        connstr = DEFAULT_CONNSTR;
    }
    if (argc > 2) {
        username = argv[2];
    } else {
        username = "Administrator";
    }
    if (argc > 3) {
        password = argv[3];
    } else {
        password = "password";
    }
    lcb_createopts_create(&crst, LCB_TYPE_BUCKET);
    lcb_createopts_connstr(crst, connstr, strlen(connstr));
    lcb_createopts_credentials(crst, username, strlen(username), password, strlen(password));

    lcb_INSTANCE *instance;
    check(lcb_create(&instance, crst));
    lcb_createopts_destroy(crst);

    check(lcb_connect(instance));

    lcb_wait(instance, LCB_WAIT_DEFAULT);

    check(lcb_get_bootstrap_status(instance));

    lcb_install_callback(instance, LCB_CALLBACK_STORE, reinterpret_cast<lcb_RESPCALLBACK>(store_callback));
    lcb_install_callback(instance, LCB_CALLBACK_GET, reinterpret_cast<lcb_RESPCALLBACK>(get_callback));
    lcb_install_callback(instance, LCB_CALLBACK_SDLOOKUP, reinterpret_cast<lcb_RESPCALLBACK>(subdoc_callback));
    lcb_install_callback(instance, LCB_CALLBACK_SDMUTATE, reinterpret_cast<lcb_RESPCALLBACK>(subdoc_callback));

    // Store the initial document. Subdocument operations cannot create
    // documents
    std::cout << "Storing the initial item..\n";
    // Store an item
    lcb_CMDSTORE *scmd;
    lcb_cmdstore_create(&scmd, LCB_STORE_UPSERT);
    lcb_cmdstore_key(scmd, "key", 3);
    const char *initval = R"({"hello":"world"})";
    lcb_cmdstore_value(scmd, initval, strlen(initval));
    lcb_STATUS rc = lcb_store(instance, nullptr, scmd);
    lcb_cmdstore_destroy(scmd);
    if (rc != LCB_SUCCESS) {
        std::cerr << "Failed to schedule store operation: " << lcb_strerror_short(rc) << "\n";
        return 1;
    }
    lcb_wait(instance, LCB_WAIT_DEFAULT);

    lcb_CMDSUBDOC *cmd;
    lcb_SUBDOCSPECS *ops;

    lcb_cmdsubdoc_create(&cmd);
    lcb_cmdsubdoc_key(cmd, "key", 3);

    /**
     * Retrieve a single item from a document
     */
    std::cout << "Getting the 'hello' path from the document\n";
    lcb_subdocspecs_create(&ops, 1);
    lcb_subdocspecs_get(ops, 0, 0, "hello", 5);
    lcb_cmdsubdoc_specs(cmd, ops);
    rc = lcb_subdoc(instance, nullptr, cmd);
    lcb_subdocspecs_destroy(ops);
    if (rc != LCB_SUCCESS) {
        std::cerr << "Failed to schedule subdocument operation: " << lcb_strerror_short(rc) << "\n";
        return 1;
    }
    lcb_wait(instance, LCB_WAIT_DEFAULT);

    /**
     * Set a dictionary/object field
     */
    std::cout << "Adding new 'goodbye' path to document\n";
    lcb_subdocspecs_create(&ops, 1);
    lcb_subdocspecs_dict_upsert(ops, 0, 0, "goodbye", 7, "\"hello\"", 7);
    lcb_cmdsubdoc_specs(cmd, ops);
    rc = lcb_subdoc(instance, nullptr, cmd);
    lcb_subdocspecs_destroy(ops);
    if (rc != LCB_SUCCESS) {
        std::cerr << "Failed to schedule subdocument operation: " << lcb_strerror_short(rc) << "\n";
        return 1;
    }
    lcb_wait(instance, LCB_WAIT_DEFAULT);
    demoKey(instance, "key");

    /**
     * Add new element to end of an array
     */
    // Options can also be used
    // tag::array-append[]
    std::cout << "Appending element to array (array might be missing)\n";
    lcb_subdocspecs_create(&ops, 1);
    // Create the array if it doesn't exist. This option can be used with
    // other commands as well..
    lcb_subdocspecs_array_add_last(ops, 0, LCB_SUBDOCSPECS_F_MKINTERMEDIATES, "array", 5, "1", 1);
    lcb_cmdsubdoc_specs(cmd, ops);
    rc = lcb_subdoc(instance, nullptr, cmd);
    lcb_subdocspecs_destroy(ops);
    if (rc != LCB_SUCCESS) {
        std::cerr << "Failed to schedule subdocument operation: " << lcb_strerror_short(rc) << "\n";
        return 1;
    }
    lcb_wait(instance, LCB_WAIT_DEFAULT);
    demoKey(instance, "key");
    // end::array-append[]
    /**
     * Add element to the beginning of an array
     */
    // tag::array-prepend[]
    std::cout << "Prepending element to array (array must exist)\n";
    lcb_subdocspecs_create(&ops, 1);
    lcb_subdocspecs_array_add_first(ops, 0, 0, "array", 5, "1", 1);
    lcb_cmdsubdoc_specs(cmd, ops);
    rc = lcb_subdoc(instance, nullptr, cmd);
    lcb_subdocspecs_destroy(ops);
    if (rc != LCB_SUCCESS) {
        std::cerr << "Failed to schedule subdocument operation: " << lcb_strerror_short(rc) << "\n";
        return 1;
    }
    lcb_wait(instance, LCB_WAIT_DEFAULT);
    demoKey(instance, "key");
    // end::array-prepend[]
    /**
     * Add unique element to the beginning of an array
     */
    // tag::array-unique[]
    lcb_subdocspecs_create(&ops, 1);
    lcb_subdocspecs_array_add_unique(ops, 0, LCB_SUBDOCSPECS_F_MKINTERMEDIATES, "a", strlen("a"), "1", strlen("1"));
    lcb_cmdsubdoc_specs(cmd, ops);
    rc = lcb_subdoc(instance, nullptr, cmd);
    lcb_subdocspecs_destroy(ops);
    if (rc != LCB_SUCCESS) {
        std::cerr << "Failed to schedule subdocument operation: " << lcb_strerror_short(rc) << "\n";
        return 1;
    }
    lcb_wait(instance, LCB_WAIT_DEFAULT);
    demoKey(instance, "key");
    // end::array-unique[]

    /**
     * Get the first element back..
     */
    std::cout << "Getting first array element...\n";
    lcb_subdocspecs_create(&ops, 1);
    lcb_subdocspecs_get(ops, 0, 0, "array[0]", 8);
    lcb_cmdsubdoc_specs(cmd, ops);
    rc = lcb_subdoc(instance, nullptr, cmd);
    lcb_subdocspecs_destroy(ops);
    if (rc != LCB_SUCCESS) {
        std::cerr << "Failed to schedule subdocument operation: " << lcb_strerror_short(rc) << "\n";
        return 1;
    }
    lcb_wait(instance, LCB_WAIT_DEFAULT);

    lcb_destroy(instance);
    return 0;
}
