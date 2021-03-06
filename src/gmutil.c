/* Copyright 2020 Jaakko Keränen <jaakko.keranen@iki.fi>

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. */

#include "gmutil.h"

#include <the_Foundation/regexp.h>
#include <the_Foundation/object.h>
#include <the_Foundation/path.h>

void init_Url(iUrl *d, const iString *text) {
    /* Handle "file:" as a special case since it only has the path part. */
    if (startsWithCase_String(text, "file://")) {
        iZap(*d);
        const char *cstr = constBegin_String(text);
        d->scheme = (iRangecc){ cstr, cstr + 4 };
        d->path   = (iRangecc){ cstr + 7, constEnd_String(text) };
        return;
    }
    static iRegExp *urlPattern_;
    static iRegExp *authPattern_;
    if (!urlPattern_) {
        urlPattern_  = new_RegExp("^(([^:/?#]+):)?(//([^/?#]*))?"
                                 "([^?#]*)(\\?([^#]*))?(#(.*))?",
                                 caseInsensitive_RegExpOption);
        authPattern_ = new_RegExp("(([^@]+)@)?(([^:\\[\\]]+)"
                                  "|(\\[[0-9a-f:]+\\]))(:([0-9]+))?",
                                  caseInsensitive_RegExpOption);
    }
    iZap(*d);
    iRegExpMatch m;
    init_RegExpMatch(&m);
    if (matchString_RegExp(urlPattern_, text, &m)) {
        d->scheme = capturedRange_RegExpMatch(&m, 2);
        d->host   = capturedRange_RegExpMatch(&m, 4);
        d->port   = (iRangecc){ d->host.end, d->host.end };
        d->path   = capturedRange_RegExpMatch(&m, 5);
        d->query  = capturedRange_RegExpMatch(&m, 6);
        /* Check if the authority contains a port. */
        init_RegExpMatch(&m);
        if (matchRange_RegExp(authPattern_, d->host, &m)) {
            d->host = capturedRange_RegExpMatch(&m, 3);
            d->port = capturedRange_RegExpMatch(&m, 7);
        }
    }
}

static iRangecc dirPath_(iRangecc path) {
    const size_t pos = lastIndexOfCStr_Rangecc(path, "/");
    if (pos == iInvalidPos) return path;
    return (iRangecc){ path.start, path.start + pos };
}

iLocalDef iBool isDef_(iRangecc cc) {
    return !isEmpty_Range(&cc);
}

static iRangecc prevPathSeg_(const char *end, const char *start) {
    iRangecc seg = { end, end };
    do {
        seg.start--;
    } while (*seg.start != '/' && seg.start != start);
    return seg;
}

void cleanUrlPath_String(iString *d) {
    iString clean;
    init_String(&clean);
    iUrl parts;
    init_Url(&parts, d);
    iRangecc seg = iNullRange;
    while (nextSplit_Rangecc(parts.path, "/", &seg)) {
        if (equal_Rangecc(seg, "..")) {
            /* Back up one segment. */
            iRangecc last = prevPathSeg_(constEnd_String(&clean), constBegin_String(&clean));
            truncate_Block(&clean.chars, last.start - constBegin_String(&clean));
        }
        else if (equal_Rangecc(seg, ".")) {
            /* Skip it. */
        }
        else if (!isEmpty_Range(&seg)) {
            /* Ensure the cleaned path starts with a slash if the original does. */
            if (!isEmpty_String(&clean) || startsWith_Rangecc(parts.path, "/")) {
                appendCStr_String(&clean, "/");
            }
            appendRange_String(&clean, seg);
        }
    }
    if (endsWith_Rangecc(parts.path, "/")) {
        appendCStr_String(&clean, "/");
    }
    /* Replace with the new path. */
    if (cmpCStrNSc_Rangecc(parts.path, cstr_String(&clean), size_String(&clean), &iCaseSensitive)) {
        const size_t pos = parts.path.start - constBegin_String(d);
        remove_Block(&d->chars, pos, size_Range(&parts.path));
        insertData_Block(&d->chars, pos, cstr_String(&clean), size_String(&clean));
    }
    deinit_String(&clean);
}

iRangecc urlScheme_String(const iString *d) {
    iUrl url;
    init_Url(&url, d);
    return url.scheme;
}

iRangecc urlHost_String(const iString *d) {
    iUrl url;
    init_Url(&url, d);
    return url.host;
}

static iBool isAbsolutePath_(iRangecc path) {
    return isAbsolute_Path(collect_String(urlDecode_String(collect_String(newRange_String(path)))));
}

const iString *absoluteUrl_String(const iString *d, const iString *urlMaybeRelative) {
    iUrl orig;
    iUrl rel;
    init_Url(&orig, d);
    init_Url(&rel, urlMaybeRelative);
    if (equalCase_Rangecc(rel.scheme, "data") || equalCase_Rangecc(rel.scheme, "about") ||
        equalCase_Rangecc(rel.scheme, "mailto")) {
        /* Special case, the contents should be left unparsed. */
        return urlMaybeRelative;
    }
    const iBool isRelative = !isDef_(rel.host);
    iRangecc scheme = range_CStr("gemini");
    if (isDef_(rel.scheme)) {
        scheme = rel.scheme;
    }
    else if (isRelative && isDef_(orig.scheme)) {
        scheme = orig.scheme;
    }
    iString *absolute = collectNew_String();
    appendRange_String(absolute, scheme);
    appendCStr_String(absolute, "://"); {
        const iUrl *selHost = isDef_(rel.host) ? &rel : &orig;
        appendRange_String(absolute, selHost->host);
        if (!isEmpty_Range(&selHost->port)) {
            appendCStr_String(absolute, ":");
            appendRange_String(absolute, selHost->port);
        }
    }
    if (isDef_(rel.scheme) || isDef_(rel.host) || isAbsolutePath_(rel.path)) {
        if (!startsWith_Rangecc(rel.path, "/")) {
            appendCStr_String(absolute, "/");
        }
        appendRange_String(absolute, rel.path);
    }
    else if (isDef_(rel.path)) {
        if (!endsWith_Rangecc(orig.path, "/")) {
            /* Referencing a file. */
            appendRange_String(absolute, dirPath_(orig.path));
        }
        else {
            /* Referencing a directory. */
            appendRange_String(absolute, orig.path);
        }
        if (!endsWith_String(absolute, "/")) {
            appendCStr_String(absolute, "/");
        }
        appendRange_String(absolute, rel.path);
    }
    else if (isDef_(rel.query)) {
        /* Just a new query. */
        appendRange_String(absolute, orig.path);
    }
    appendRange_String(absolute, rel.query);
    cleanUrlPath_String(absolute);
    return absolute;
}

iString *makeFileUrl_String(const iString *localFilePath) {
    iString *url = cleaned_Path(localFilePath);
    replace_Block(&url->chars, '\\', '/'); /* in case it's a Windows path */
    set_String(url, collect_String(urlEncodeExclude_String(url, "/")));
    prependCStr_String(url, "file://");
    return url;
}

const char *makeFileUrl_CStr(const char *localFilePath) {
    return cstrCollect_String(makeFileUrl_String(collectNewCStr_String(localFilePath)));
}

void urlEncodeSpaces_String(iString *d) {
    for (;;) {
        const size_t pos = indexOfCStr_String(d, " ");
        if (pos == iInvalidPos) break;
        remove_Block(&d->chars, pos, 1);
        insertData_Block(&d->chars, pos, "%20", 3);
    }
}

static const struct {
    enum iGmStatusCode code;
    iGmError           err;
} errors_[] = {
    { unknownStatusCode_GmStatusCode, /* keep this as the first one (fallback return value) */
      { 0x1f4ab, /* dizzy */
        "Unknown Status Code",
        "The server responded with a status code that is not in the Gemini specification. "
        "Maybe the server is from the future? Or just malfunctioning." } },
    { failedToOpenFile_GmStatusCode,
      { 0x1f4c1, /* file folder */
        "Failed to Open File",
        "The requested file does not exist or is inaccessible. "
        "Please check the file path." } },
    { invalidLocalResource_GmStatusCode,
      { 0,
        "Invalid Resource",
        "The requested resource does not exist." } },
    { unsupportedMimeType_GmStatusCode,
      { 0x1f47d, /* alien */
        "Unsupported Content Type",
        "The received content cannot be viewed with this application." } },
    { unsupportedProtocol_GmStatusCode,
      { 0x1f61e, /* disappointed */
        "Unsupported Protocol",
        "The requested protocol is not supported by this application." },
    },
    { invalidHeader_GmStatusCode,
      { 0x1f4a9, /* pile of poo */
        "Invalid Header",
        "The received header did not conform to the Gemini specification. "
        "Perhaps the server is malfunctioning or you tried to contact a "
        "non-Gemini server." } },
    { invalidRedirect_GmStatusCode,
      { 0x27a0, /* dashed arrow */
        "Invalid Redirect",
        "The server responded with a redirect but did not provide a valid destination URL. "
        "Perhaps the server is malfunctioning." } },
    { schemeChangeRedirect_GmStatusCode,
      { 0x27a0, /* dashed arrow */
        "Scheme-Changing Redirect",
        "The server attempted to redirect us to a URL whose scheme is different than the "
        "originating URL's scheme. Here is the link so you can open it manually if appropriate."} },
    { tooManyRedirects_GmStatusCode,
      { 0x27a0, /* dashed arrow */
        "Too Many Redirects",
        "You may be stuck in a redirection loop. The next redirected URL is below if you "
        "want to continue manually."} },
    { tlsFailure_GmStatusCode,
      { 0x1f5a7, /* networked computers */
        "Network/TLS Failure",
        "Failed to communicate with the host. Here is the error message:" } },
    { temporaryFailure_GmStatusCode,
      { 0x1f50c, /* electric plug */
        "Temporary Failure",
        "The request has failed, but may succeed if you try again in the future." } },
    { serverUnavailable_GmStatusCode,
      { 0x1f525, /* fire */
        "Server Unavailable",
        "The server is unavailable due to overload or maintenance. Check back later." } },
    { cgiError_GmStatusCode,
      { 0x1f4a5, /* collision */
        "CGI Error",
        "Failure during dynamic content generation on the server. This may be due "
        "to buggy serverside software." } },
    { proxyError_GmStatusCode,
      { 0x1f310, /* globe */
        "Proxy Error",
        "A proxy request failed because the server was unable to successfully "
        "complete a transaction with the remote host. Perhaps there are difficulties "
        "with network connectivity." } },
    { slowDown_GmStatusCode,
      { 0x1f40c, /* snail */
        "Slow Down",
        "The server is rate limiting requests. Please wait..." } },
    { permanentFailure_GmStatusCode,
      { 0x1f6ab, /* no entry */
        "Permanent Failure",
        "Your request has failed and will fail in the future as well if repeated." } },
    { notFound_GmStatusCode,
      { 0x1f50d, /* magnifying glass */
        "Not Found",
        "The requested resource could not be found at this time." } },
    { gone_GmStatusCode,
      { 0x1f47b, /* ghost */
        "Gone",
        "The resource requested is no longer available and will not be available again." } },
    { proxyRequestRefused_GmStatusCode,
      { 0x1f6c2, /* passport control */
        "Proxy Request Refused",
        "The request was for a resource at a domain not served by the server and the "
        "server does not accept proxy requests." } },
    { badRequest_GmStatusCode,
      { 0x1f44e, /* thumbs down */
        "Bad Request",
        "The server was unable to parse your request, presumably due to the "
        "request being malformed." } },
    { clientCertificateRequired_GmStatusCode,
      { 0x1f511, /* key */
        "Certificate Required",
        "Access to the requested resource requires identification via "
        "a client certificate." } },
    { certificateNotAuthorized_GmStatusCode,
      { 0x1f512, /* lock */
        "Certificate Not Authorized",
        "The provided client certificate is valid but is not authorized for accessing "
        "the requested resource. " } },
    { certificateNotValid_GmStatusCode,
      { 0x1f6a8, /* revolving light */
        "Invalid Certificate",
        "The provided client certificate is expired or invalid." } },
};

iBool isDefined_GmError(enum iGmStatusCode code) {
    iForIndices(i, errors_) {
        if (errors_[i].code == code) {
            return iTrue;
        }
    }
    return iFalse;
}

const iGmError *get_GmError(enum iGmStatusCode code) {
    static const iGmError none = { 0, "", "" };
    if (code == 0) {
        return &none;
    }
    iForIndices(i, errors_) {
        if (errors_[i].code == code) {
            return &errors_[i].err;
        }
    }
    iAssert(errors_[0].code == unknownStatusCode_GmStatusCode);
    return &errors_[0].err; /* unknown */
}
