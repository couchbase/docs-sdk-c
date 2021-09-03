#include <libcouchbase/couchbase.h>
#include <string>
#include <iostream>

static void
check(lcb_STATUS err, const char *msg) {
    if (err != LCB_SUCCESS) {
        std::cerr << "[ERROR] " << msg << ": " << lcb_strerror_short(err) << "\n";
        exit(EXIT_FAILURE);
    }
}

void
http_callback(lcb_INSTANCE *, int, const lcb_RESPHTTP *resp) {
    check(lcb_resphttp_status(resp), "HTTP operation status in the callback");

    uint16_t status;
    lcb_resphttp_http_status(resp, &status);
    std::cout << "HTTP status: " << status << "\n";

    const char *buf;
    size_t buf_len = 0;
    lcb_resphttp_body(resp, &buf, &buf_len);
    if (buf_len > 0) {
        std::cout << std::string(buf, buf_len) << "\n";
    }
}

int
main() {
    std::string username{"some-user"};
    std::string password{"some-password"};
    std::string connection_string{"couchbase://localhost"};

    lcb_CREATEOPTS *create_options = nullptr;
    check(lcb_createopts_create(&create_options, LCB_TYPE_CLUSTER),
          "build options object for lcb_create in CLUSTER mode");
    check(lcb_createopts_credentials(create_options, username.data(), username.size(),
                                     password.data(),
                                     password.size()),
          "assign credentials");
    check(lcb_createopts_connstr(create_options, connection_string.data(),
                                 connection_string.size()),
          "assign connection string");

    lcb_INSTANCE *instance = nullptr;
    check(lcb_create(&instance, create_options), "create lcb_INSTANCE");
    check(lcb_createopts_destroy(create_options), "destroy options object");
    check(lcb_connect(instance), "schedule connection");
    check(lcb_wait(instance, LCB_WAIT_DEFAULT), "wait for connection");
    check(lcb_get_bootstrap_status(instance), "check bootstrap status");

    /**
     * More on bucket create REST API
     * https://docs.couchbase.com/server/current/rest-api/rest-bucket-create.html
     */
    std::string bucket_name{"mybucket"};
    std::size_t bucket_ram_quota_in_mb{200};

    // The settings must be encoded as URL form
    std::string content_type{"application/x-www-form-urlencoded"};
    std::string bucket_settings{
            "name=" + bucket_name + "&ramQuotaMB=" + std::to_string(bucket_ram_quota_in_mb)};

    std::string create_path{"/pools/default/buckets"};
    lcb_install_callback(instance, LCB_CALLBACK_HTTP,
                         reinterpret_cast<lcb_RESPCALLBACK>(http_callback));

    lcb_CMDHTTP *cmd = nullptr;
    check(lcb_cmdhttp_create(&cmd, LCB_HTTP_TYPE_MANAGEMENT),
          "create HTTP command object of MANAGEMENT type");
    check(lcb_cmdhttp_method(cmd, LCB_HTTP_METHOD_POST), "set HTTP method");
    check(lcb_cmdhttp_path(cmd, create_path.data(), create_path.size()), "set HTTP path");
    check(lcb_cmdhttp_content_type(cmd, content_type.data(), content_type.size()),
          "set HTTP content type");
    check(lcb_cmdhttp_body(cmd, bucket_settings.data(), bucket_settings.size()), "set HTTP body");
    check(lcb_http(instance, nullptr, cmd), "schedule HTTP command");
    check(lcb_cmdhttp_destroy(cmd), "destroy command object");

    check(lcb_wait(instance, LCB_WAIT_DEFAULT), "wait for completion");

    lcb_destroy(instance);
    return 0;
}

// OUTPUT (success)
//
// HTTP status: 202
//

// OUTPUT (already exists)
//
// HTTP status: 400
// {
//   "errors": {
//     "name": "Bucket with given name already exists"
//   },
//   "summaries": {
//     "ramSummary": {
//       "total": 536870912,
//       "otherBuckets": 209715200,
//       "nodesCount": 1,
//       "perNodeMegs": 200,
//       "thisAlloc": 209715200,
//       "thisUsed": 0,
//       "free": 117440512
//     },
//     "hddSummary": {
//       "total": 228026265600,
//       "otherData": 221181213938,
//       "otherBuckets": 4263694,
//       "thisUsed": 0,
//       "free": 6840787968
//     }
//   }
// }
