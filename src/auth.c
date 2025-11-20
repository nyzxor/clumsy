#include <Windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <string.h>
#include "iup.h"
#include "common.h"

#pragma comment(lib, "winhttp.lib")

// KeyAuth configuration - UPDATE THESE WITH YOUR KEYAUTH APP DETAILS
#define KEYAUTH_NAME "synet"           // Your KeyAuth application name
#define KEYAUTH_OWNERID "OWNER_ID"     // Your KeyAuth owner ID
#define KEYAUTH_SECRET "APP_SECRET"    // Your KeyAuth application secret
#define KEYAUTH_VERSION "1.0"          // Application version

static char sessionId[64] = {0};
static BOOL isAuthenticated = FALSE;

// Simple URL encoding for special characters
static void urlEncode(const char* src, char* dest, size_t destSize) {
    size_t i, j = 0;
    for (i = 0; src[i] && j < destSize - 4; i++) {
        char c = src[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            dest[j++] = c;
        } else {
            sprintf(dest + j, "%%%02X", (unsigned char)c);
            j += 3;
        }
    }
    dest[j] = '\0';
}

// Generate a simple HWID based on system info
static void getHWID(char* hwid, size_t size) {
    char computerName[MAX_COMPUTERNAME_LENGTH + 1];
    char userName[256];
    DWORD cnSize = sizeof(computerName);
    DWORD unSize = sizeof(userName);

    GetComputerNameA(computerName, &cnSize);
    GetUserNameA(userName, &unSize);

    // Simple HWID - combine computer name and username
    snprintf(hwid, size, "%s-%s", computerName, userName);
}

// Make HTTP POST request to KeyAuth API
static BOOL keyauthRequest(const char* endpoint, const char* postData, char* response, size_t responseSize) {
    HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;
    BOOL result = FALSE;
    DWORD bytesRead = 0;
    DWORD totalRead = 0;

    hSession = WinHttpOpen(L"Synet/1.0",
                           WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                           WINHTTP_NO_PROXY_NAME,
                           WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) goto cleanup;

    hConnect = WinHttpConnect(hSession, L"keyauth.win",
                              INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) goto cleanup;

    hRequest = WinHttpOpenRequest(hConnect, L"POST",
                                  L"/api/1.2/",
                                  NULL, WINHTTP_NO_REFERER,
                                  WINHTTP_DEFAULT_ACCEPT_TYPES,
                                  WINHTTP_FLAG_SECURE);
    if (!hRequest) goto cleanup;

    // Set headers
    WinHttpAddRequestHeaders(hRequest,
        L"Content-Type: application/x-www-form-urlencoded",
        -1, WINHTTP_ADDREQ_FLAG_ADD);

    // Send request
    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            (LPVOID)postData, (DWORD)strlen(postData),
                            (DWORD)strlen(postData), 0)) {
        goto cleanup;
    }

    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        goto cleanup;
    }

    // Read response
    while (WinHttpReadData(hRequest, response + totalRead,
                           (DWORD)(responseSize - totalRead - 1), &bytesRead)) {
        if (bytesRead == 0) break;
        totalRead += bytesRead;
    }
    response[totalRead] = '\0';
    result = TRUE;

cleanup:
    if (hRequest) WinHttpCloseHandle(hRequest);
    if (hConnect) WinHttpCloseHandle(hConnect);
    if (hSession) WinHttpCloseHandle(hSession);

    return result;
}

// Parse simple JSON value (basic implementation)
static BOOL parseJsonValue(const char* json, const char* key, char* value, size_t valueSize) {
    char searchKey[128];
    const char* pos;
    const char* start;
    const char* end;

    snprintf(searchKey, sizeof(searchKey), "\"%s\"", key);
    pos = strstr(json, searchKey);
    if (!pos) return FALSE;

    pos = strchr(pos, ':');
    if (!pos) return FALSE;
    pos++;

    // Skip whitespace
    while (*pos == ' ' || *pos == '\t') pos++;

    if (*pos == '"') {
        // String value
        start = pos + 1;
        end = strchr(start, '"');
        if (!end) return FALSE;
    } else {
        // Boolean or other value
        start = pos;
        end = start;
        while (*end && *end != ',' && *end != '}' && *end != ' ') end++;
    }

    size_t len = end - start;
    if (len >= valueSize) len = valueSize - 1;
    strncpy(value, start, len);
    value[len] = '\0';

    return TRUE;
}

// Initialize KeyAuth session
static BOOL keyauthInit(void) {
    char postData[1024];
    char response[4096];
    char success[16];

    snprintf(postData, sizeof(postData),
        "type=init&ver=%s&name=%s&ownerid=%s",
        KEYAUTH_VERSION, KEYAUTH_NAME, KEYAUTH_OWNERID);

    if (!keyauthRequest("/api/1.2/", postData, response, sizeof(response))) {
        return FALSE;
    }

    if (parseJsonValue(response, "success", success, sizeof(success))) {
        if (strcmp(success, "true") == 0) {
            parseJsonValue(response, "sessionid", sessionId, sizeof(sessionId));
            return TRUE;
        }
    }

    return FALSE;
}

// Validate license key with KeyAuth
static BOOL keyauthLicense(const char* key) {
    char postData[1024];
    char response[4096];
    char success[16];
    char hwid[256];
    char encodedKey[512];
    char encodedHwid[512];

    getHWID(hwid, sizeof(hwid));
    urlEncode(key, encodedKey, sizeof(encodedKey));
    urlEncode(hwid, encodedHwid, sizeof(encodedHwid));

    snprintf(postData, sizeof(postData),
        "type=license&key=%s&hwid=%s&sessionid=%s&name=%s&ownerid=%s",
        encodedKey, encodedHwid, sessionId, KEYAUTH_NAME, KEYAUTH_OWNERID);

    if (!keyauthRequest("/api/1.2/", postData, response, sizeof(response))) {
        return FALSE;
    }

    if (parseJsonValue(response, "success", success, sizeof(success))) {
        if (strcmp(success, "true") == 0) {
            isAuthenticated = TRUE;
            return TRUE;
        }
    }

    return FALSE;
}

// Dialog callbacks
static Ihandle *keyText = NULL;
static Ihandle *authDialog = NULL;
static BOOL authSuccess = FALSE;

static int onActivateKey(Ihandle *ih) {
    const char* key;
    UNREFERENCED_PARAMETER(ih);

    key = IupGetAttribute(keyText, "VALUE");
    if (!key || strlen(key) < 5) {
        IupMessage("Error", "Please enter a valid license key.");
        return IUP_DEFAULT;
    }

    // Initialize KeyAuth session
    if (!keyauthInit()) {
        IupMessage("Error", "Failed to connect to license server.\nPlease check your internet connection.");
        return IUP_DEFAULT;
    }

    // Validate license
    if (keyauthLicense(key)) {
        authSuccess = TRUE;
        return IUP_CLOSE;
    } else {
        IupMessage("Error", "Invalid license key or key already in use.");
        return IUP_DEFAULT;
    }
}

static int onCancel(Ihandle *ih) {
    UNREFERENCED_PARAMETER(ih);
    authSuccess = FALSE;
    return IUP_CLOSE;
}

static int onDialogClose(Ihandle *ih) {
    UNREFERENCED_PARAMETER(ih);
    if (!authSuccess) {
        return IUP_CLOSE;
    }
    return IUP_DEFAULT;
}

// Show authentication dialog
BOOL showAuthDialog(void) {
    Ihandle *vbox, *hbox, *activateBtn, *cancelBtn, *label;

    label = IupLabel("Enter your license key:");
    IupSetAttribute(label, "PADDING", "0x4");

    keyText = IupText(NULL);
    IupSetAttribute(keyText, "EXPAND", "HORIZONTAL");
    IupSetAttribute(keyText, "VISIBLECOLUMNS", "30");
    IupSetAttribute(keyText, "PASSWORD", "YES");

    activateBtn = IupButton("Activate", NULL);
    IupSetAttribute(activateBtn, "PADDING", "16x4");
    IupSetAttribute(activateBtn, "BGCOLOR", "56 142 60");
    IupSetAttribute(activateBtn, "FGCOLOR", "255 255 255");
    IupSetCallback(activateBtn, "ACTION", (Icallback)onActivateKey);

    cancelBtn = IupButton("Exit", NULL);
    IupSetAttribute(cancelBtn, "PADDING", "16x4");
    IupSetCallback(cancelBtn, "ACTION", (Icallback)onCancel);

    hbox = IupHbox(
        IupFill(),
        activateBtn,
        cancelBtn,
        NULL
    );
    IupSetAttribute(hbox, "GAP", "8");
    IupSetAttribute(hbox, "ALIGNMENT", "ACENTER");

    vbox = IupVbox(
        label,
        keyText,
        hbox,
        NULL
    );
    IupSetAttribute(vbox, "MARGIN", "16x16");
    IupSetAttribute(vbox, "GAP", "8");

    authDialog = IupDialog(vbox);
    IupSetAttribute(authDialog, "TITLE", "Synet - License Activation");
    IupSetAttribute(authDialog, "DIALOGFRAME", "YES");
    IupSetAttribute(authDialog, "RESIZE", "NO");
    IupSetAttribute(authDialog, "BGCOLOR", "250 250 250");
    IupSetCallback(authDialog, "CLOSE_CB", (Icallback)onDialogClose);

    IupPopup(authDialog, IUP_CENTER, IUP_CENTER);
    IupDestroy(authDialog);

    return authSuccess;
}

BOOL isLicenseValid(void) {
    return isAuthenticated;
}
