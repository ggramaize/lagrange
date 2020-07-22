#pragma once

/* Response status codes. */
enum iGmStatusCode {
    none_GmStatusCode                      = 0,
    input_GmStatusCode                     = 10,
    sensitiveInput_GmStatusCode            = 11,
    success_GmStatusCode                   = 20,
    redirectTemporary_GmStatusCode         = 30,
    redirectPermanent_GmStatusCode         = 31,
    temporaryFailure_GmStatusCode          = 40,
    serverUnavailable_GmStatusCode         = 41,
    cgiError_GmStatusCode                  = 42,
    proxyError_GmStatusCode                = 43,
    slowDown_GmStatusCode                  = 44,
    permanentFailure_GmStatusCode          = 50,
    notFound_GmStatusCode                  = 51,
    gone_GmStatusCode                      = 52,
    proxyRequestRefused_GmStatusCode       = 53,
    badRequest_GmStatusCode                = 59,
    clientCertificateRequired_GmStatusCode = 60,
    certificateNotAuthorized_GmStatusCode  = 61,
    certificateNotValid_GmStatusCode       = 62,
};