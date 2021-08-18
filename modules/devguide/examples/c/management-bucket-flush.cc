#include <string>
#include <iostream>

#include <libcouchbase/couchbase.h>

static void
check(lcb_STATUS err, const char *msg)
{
    if (err != LCB_SUCCESS) {
        std::cerr << "[ERROR] " << msg << ": " << lcb_strerror_short(err) << "\n";
        exit(EXIT_FAILURE);
    }
}

void
http_callback(lcb_INSTANCE *, int, const lcb_RESPHTTP *resp)
{
    check(lcb_resphttp_status(resp), "HTTP operation status in the callback");

    uint16_t status;
    lcb_resphttp_http_status(resp, &status);
    std::cout << "HTTP status: " << status << "\n";

    const char *buf;
    size_t buf_len = 0;
    lcb_resphttp_body(resp, &buf, &buf_len);
    if (buf_len > 0) {
        std::string body(buf, buf_len);
        if (body.find("Flush is disabled") != std::string::npos) {
            std::cerr << "[ERROR] Flush is not enabled for the specified bucket\n";
            return;
        }
        std::cout << body << "\n";
    }
}

int
main()
{
    std::string username{"some-user"};
    std::string password{"some-password"};
    std::string connection_string{"couchbase://localhost"};

    lcb_CREATEOPTS *create_options = nullptr;
    check(lcb_createopts_create(&create_options, LCB_TYPE_CLUSTER),
            "build options object for lcb_create in CLUSTER mode");
    check(lcb_createopts_credentials(create_options, username.c_str(), username.size(),
                    password.c_str(),
                    password.size()),
            "assign credentials");
    check(lcb_createopts_connstr(create_options, connection_string.c_str(),
                    connection_string.size()),
            "assign connection string");

    lcb_INSTANCE *instance = nullptr;
    check(lcb_create(&instance, create_options), "create lcb_INSTANCE");
    check(lcb_createopts_destroy(create_options), "destroy options object");
    check(lcb_connect(instance), "schedule connection");
    check(lcb_wait(instance, LCB_WAIT_DEFAULT), "wait for connection");
    check(lcb_get_bootstrap_status(instance), "check bootstrap status");

    /**
     * More on bucket flush REST API
     * https://docs.couchbase.com/server/current/rest-api/rest-bucket-flush.html
     */
    std::string bucket_name{"travel-sample"};
    std::cout << "Flushing bucket \"" << bucket_name << "\"\n";

    std::string flush_path{"/pools/default/buckets/" + bucket_name + "/controller/doFlush"};
    lcb_install_callback(instance, LCB_CALLBACK_HTTP,
            reinterpret_cast<lcb_RESPCALLBACK>(http_callback));

    lcb_CMDHTTP *cmd = nullptr;
    check(lcb_cmdhttp_create(&cmd, LCB_HTTP_TYPE_MANAGEMENT),
            "create HTTP command object of MANAGEMENT type");
    check(lcb_cmdhttp_method(cmd, LCB_HTTP_METHOD_POST), "set HTTP method");
    check(lcb_cmdhttp_path(cmd, flush_path.c_str(), flush_path.size()), "set HTTP path");
    check(lcb_http(instance, nullptr, cmd), "schedule HTTP command");
    check(lcb_cmdhttp_destroy(cmd), "destroy command object");

    check(lcb_wait(instance, LCB_WAIT_DEFAULT), "wait for completion");

    lcb_destroy(instance);
    return 0;
}

// OUTPUT (flush is not enabled)
//
// Flushing bucket "travel-sample"
// HTTP status: 400
// [ERROR] Flush is not enable for the specified bucket

// OUTPUT (success)
//
// Flushing bucket "travel-sample"
// HTTP status: 200

// OUTPUT (bucket missing)
//
// Flushing bucket "travel-sample"
// HTTP status: 404
// Requested resource not found.
