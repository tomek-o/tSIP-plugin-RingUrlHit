//---------------------------------------------------------------------------

#define _EXPORTING
#include "..\tSIP\tSIP\phone\Phone.h"
#include "..\tSIP\tSIP\phone\PhoneSettings.h"
#include "..\tSIP\tSIP\phone\PhoneCapabilities.h"
#include "Log.h"
#include <assert.h>
#include <algorithm>	// needed by Utils::in_group
#include "Utils.h"
#include <string>
#include <fstream>
#include <json/json.h>
#include <wininet.h>

//---------------------------------------------------------------------------

static const struct S_PHONE_DLL_INTERFACE dll_interface =
{DLL_INTERFACE_MAJOR_VERSION, DLL_INTERFACE_MINOR_VERSION};

// callback ptrs
CALLBACK_LOG lpLogFn = NULL;
CALLBACK_CONNECT lpConnectFn = NULL;
CALLBACK_KEY lpKeyFn = NULL;


void *callbackCookie;	///< used by upper class to distinguish library instances when receiving callbacks

namespace {
std::string host = "google.com";
unsigned int port = 80;
std::string request = "/search?q=[number]";
std::string callDisplay;
int callState = 0;
bool connected = false;
bool exited = true;
}


extern "C" __declspec(dllexport) void GetPhoneInterfaceDescription(struct S_PHONE_DLL_INTERFACE* interf) {
    interf->majorVersion = dll_interface.majorVersion;
    interf->minorVersion = dll_interface.minorVersion;
}

void Log(const char* txt) {
    if (lpLogFn)
        lpLogFn(callbackCookie, const_cast<char*>(txt));
}

void Connect(int state, char *szMsgText) {
    if (lpConnectFn)
        lpConnectFn(callbackCookie, state, szMsgText);
}

void Key(int keyCode, int state) {
    if (lpKeyFn)
        lpKeyFn(callbackCookie, keyCode, state);
}

void SetCallbacks(void *cookie, CALLBACK_LOG lpLog, CALLBACK_CONNECT lpConnect, CALLBACK_KEY lpKey) {
    assert(cookie && lpLog && lpConnect && lpKey);
    lpLogFn = lpLog;
    lpConnectFn = lpConnect;
    lpKeyFn = lpKey;
    callbackCookie = cookie;

    Log("RingUrlHit plugin loaded\n");
}

void GetPhoneCapabilities(struct S_PHONE_CAPABILITIES **caps) {
    static struct S_PHONE_CAPABILITIES capabilities = {
        0
    };
    *caps = &capabilities;
}

void ShowSettings(HANDLE parent) {
    MessageBox((HWND)parent, "No additional settings.", "Device DLL", MB_ICONINFORMATION);
}

int Connect(void) {
    return 0;
}

int Disconnect(void) {
    return 0;
}

static bool bSettingsReaded = false;

static int GetDefaultSettings(struct S_PHONE_SETTINGS* settings) {
    settings->ring = 1;

    bSettingsReaded = true;
    return 0;
}

int GetPhoneSettings(struct S_PHONE_SETTINGS* settings) {

    std::string path = Utils::GetDllPath();
    path = Utils::ReplaceFileExtension(path, ".cfg");
    if (path == "")
        return GetDefaultSettings(settings);

    Json::Value root;   // will contains the root value after parsing.
    Json::Reader reader;

    std::ifstream ifs(path.c_str());
    std::string strConfig((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    ifs.close();
    //settings->iTriggerSrcChannel = 0;

    bool parsingSuccessful = reader.parse( strConfig, root );
    if ( !parsingSuccessful )
        return GetDefaultSettings(settings);

    GetDefaultSettings(settings);


    //int mode = root.get("TriggerMode", TRIGGER_MODE_AUTO).asInt();
    settings->ring = true;//root.get("ring", settings->ring).asInt();
    host = root.get("host", host.c_str()).asString();
    port = root.get("port", port).asUInt();
    request = root.get("request", request.c_str()).asString();

    bSettingsReaded = true;
    return 0;
}

int SavePhoneSettings(struct S_PHONE_SETTINGS* settings) {
    Json::Value root;
    Json::StyledWriter writer;

    //root["ring"] = settings->ring;
    root["host"] = host;
    root["port"] = port;
    root["request"] = request;



    std::string outputConfig = writer.write( root );

    std::string path = Utils::GetDllPath();
    path = Utils::ReplaceFileExtension(path, ".cfg");
    if (path == "")
        return -1;

    std::ofstream ofs(path.c_str());
    ofs << outputConfig;
    ofs.close();

    return 0;
}

int SetRegistrationState(int state) {
    return 0;
}

int SetCallState(int state, const char* display) {
    callState = state;
    callDisplay = display;
    return 0;
}

static void replaceAll( std::string &s, const std::string &search, const std::string &replace ) {
    for( size_t pos = 0; ; pos += replace.length() ) {
        // Locate the substring to replace
        pos = s.find( search, pos );
        if( pos == std::string::npos ) break;
        // Replace by erasing and inserting
        s.erase( pos, search.length() );
        s.insert( pos, replace );
    }
}

DWORD WINAPI RingThreadProc(LPVOID data) {
    // wait for SetCallState()
    for (int i=0; i<10; i++) {
        if (callState != 0)
            break;
        Sleep(100);
    }


    if (host.size() && callDisplay != "") {
        HINTERNET hInternetHandle = InternetOpen( "RingUrlHit",
                                    INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0 );
        HINTERNET hConnectHandle = NULL;
        HINTERNET hRequestHandle = NULL;

        if (hInternetHandle == NULL) {
            Log("RingUrlHit: InternetOpen failed\n");
        } else {

            DWORD dwTimeOut = 1500; // In milliseconds
            InternetSetOption( hInternetHandle, INTERNET_OPTION_CONNECT_TIMEOUT,
                               &dwTimeOut, sizeof( dwTimeOut ) );
            InternetSetOption( hInternetHandle, INTERNET_OPTION_RECEIVE_TIMEOUT,
                               &dwTimeOut, sizeof( dwTimeOut ) );
            InternetSetOption( hInternetHandle, INTERNET_OPTION_SEND_TIMEOUT,
                               &dwTimeOut, sizeof( dwTimeOut ) );
            DWORD dwRetries = 1;
            InternetSetOption( hInternetHandle, INTERNET_OPTION_CONNECT_RETRIES,
                               &dwRetries, sizeof( dwRetries ) );

            // szServerName = our server name
            hConnectHandle = InternetConnect( hInternetHandle, host.c_str(),
                                       port, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0 );
            if (hConnectHandle == NULL) {
                Log("RingUrlHit: InternetConnect failed\n");
            } else {

                // szPath = the file to download
                LPCSTR aszDefault[2] = { "text/html, */*", NULL };
                DWORD dwFlags = 0
                                | INTERNET_FLAG_NO_AUTH
                                //| INTERNET_FLAG_NO_AUTO_REDIRECT
                                | INTERNET_FLAG_NO_COOKIES
                                | INTERNET_FLAG_NO_UI
                                | INTERNET_FLAG_RELOAD;
                std::string requestWithNumber = request;
                replaceAll(requestWithNumber, "[number]", callDisplay);
                hRequestHandle = HttpOpenRequest( hConnectHandle, "GET", requestWithNumber.c_str(), NULL,
                                           NULL, aszDefault, dwFlags, 0 );
                if (hRequestHandle == NULL) {
                    Log("RingUrlHit: HttpOpenRequest failed\n");
                    int err = GetLastError();
                    char tmp[256];
                    snprintf(tmp, sizeof(tmp), "Error: %d\n", err);
                    Log(tmp);
                } else {

                    if (HttpSendRequest( hRequestHandle, NULL, 0, NULL, 0 ) == 0) {
                        int err = GetLastError();
                        Log("RingUrlHit: HttpSendRequest failed\n");
                        char tmp[256];
                        snprintf(tmp, sizeof(tmp), "Error: %d\n", err);
                        Log(tmp);
                    } else {
                        Sleep(10);
                    }
                }
            }
        }

        if (hRequestHandle)
            InternetCloseHandle(hRequestHandle);
        if (hConnectHandle)
            InternetCloseHandle(hConnectHandle);
        if (hInternetHandle)
            InternetCloseHandle(hInternetHandle);
    } else {
        //MessageBox(NULL, "Empty cmd.", "Device DLL", MB_ICONINFORMATION);
    }

    exited = true;
    return 0;
}


int RingThreadStart(void) {
    DWORD dwtid;
    exited = false;
    connected = true;
    HANDLE RingThread = CreateThread(NULL, 0, RingThreadProc, /*this*/NULL, 0, &dwtid);
    if (RingThread == NULL) {
        Log("Failed to create ring thread.");
        connected = false;
        exited = true;
    } else {
        //Log("Ring thread created.\n");
    }
    return 0;
}

int RingThreadStop(void) {
    connected = false;
    while (!exited) {
        Sleep(50);
    }
    return 0;
}

int Ring(int state) {
    if (state) {
        if (connected == false) {
            RingThreadStart();
        }
    } else {
        RingThreadStop();
    }
    return 0;
}

