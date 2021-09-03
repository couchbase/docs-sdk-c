#include <libcouchbase/couchbase.h>
#include <iostream>
#include <cassert>

static void
http_callback(lcb_INSTANCE *, int, const lcb_RESPHTTP *resp) {
    uint16_t httpStatus = 0;
    assert(lcb_resphttp_http_status(resp, &httpStatus) == LCB_SUCCESS);

    const char *body = nullptr;
    size_t bodyLen = 0;
    assert(lcb_resphttp_body(resp, &body, &bodyLen) == LCB_SUCCESS);
    std::cout << "HTTP callback with status code: " << httpStatus << std::endl;
    if (bodyLen > 0) {
        std::cout << "HTTP body: " << std::string(body, bodyLen);
    } else {
        std::cout << "(HTTP body was EMPTY)";
    }
    std::cout << std::endl;
}

int
main(int, char **) {
    std::string username = "some-user";
    std::string password = "some-password";
    std::string connection_string = "couchbase://127.0.0.1/default";

    lcb_CREATEOPTS *create_options = nullptr;
    lcb_createopts_create(&create_options, LCB_TYPE_BUCKET);
    lcb_createopts_connstr(create_options, connection_string.data(), connection_string.size());
    lcb_createopts_credentials(create_options, username.data(), username.size(), password.data(),
                               password.size());

    lcb_INSTANCE *instance;
    assert(lcb_create(&instance, create_options) == LCB_SUCCESS);
    lcb_createopts_destroy(create_options);

    // schedule and wait for the Connection command
    assert(lcb_connect(instance) == LCB_SUCCESS);
    assert(lcb_wait(instance, LCB_WAIT_DEFAULT) == LCB_SUCCESS);

    // Assert that the bootstrap/connection succeeded
    assert(lcb_get_bootstrap_status(instance) == LCB_SUCCESS);

    ////////// Create a `newBucket`

    // Install the HTTP callback handler function
    lcb_install_callback(instance, LCB_CALLBACK_HTTP, reinterpret_cast<lcb_RESPCALLBACK>(http_callback));

    lcb_CMDHTTP *cmdHttp = nullptr;
    assert(lcb_cmdhttp_create(&cmdHttp, LCB_HTTP_TYPE_MANAGEMENT) == LCB_SUCCESS);
    assert(lcb_cmdhttp_method(cmdHttp, LCB_HTTP_METHOD_POST) == LCB_SUCCESS);

    std::string bucketPath("/pools/default/buckets");
    assert(lcb_cmdhttp_path(cmdHttp, bucketPath.data(), bucketPath.size()) == LCB_SUCCESS);

    // reusing the main password as a simple example here
    assert(lcb_cmdhttp_username(cmdHttp, username.data(), username.size()) == LCB_SUCCESS);
    assert(lcb_cmdhttp_password(cmdHttp, password.data(), password.size()) == LCB_SUCCESS);

    // set the HTTP Content-Type
    std::string contentType("application/x-www-form-urlencoded");
    assert(lcb_cmdhttp_content_type(cmdHttp, contentType.data(), contentType.size()) == LCB_SUCCESS);

    // Create the required parameters for the Body according to the Couchbase REST API
    std::string params;
    params += "name=newBucket&";
    params += "bucketType=couchbase&";

    // authType should always be SASL. You can leave the saslPassword field
    // empty if you don't want to protect this bucket.
    params += "authType=sasl&saslPassword=&";
    params += "ramQuotaMB=100";
    std::cout << "HTTP Request Body Params: " << params << std::endl;

    // set the HTTP Body
    assert(lcb_cmdhttp_body(cmdHttp, params.data(), params.size()) == LCB_SUCCESS);

    // schedule and wait for the HTTP command (not destroying the command because it's being used again)
    assert(lcb_http(instance, nullptr, cmdHttp) == LCB_SUCCESS);
    assert(lcb_wait(instance, LCB_WAIT_DEFAULT) == LCB_SUCCESS);

    ////////// Remove the `newBucket`

    std::string newBucketPath("/pools/default/buckets/newBucket");
    assert(lcb_cmdhttp_method(cmdHttp, LCB_HTTP_METHOD_DELETE) == LCB_SUCCESS);
    assert(lcb_cmdhttp_path(cmdHttp, newBucketPath.data(), newBucketPath.size()) == LCB_SUCCESS);
    assert(lcb_cmdhttp_body(cmdHttp, nullptr, 0) == LCB_SUCCESS);

    // schedule and wait for the HTTP command
    assert(lcb_http(instance, nullptr, cmdHttp) == LCB_SUCCESS);
    lcb_cmdhttp_destroy(cmdHttp);
    assert(lcb_wait(instance, LCB_WAIT_DEFAULT) == LCB_SUCCESS);

    lcb_destroy(instance);
}
