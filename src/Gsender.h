/* Gsender class helps send e-mails from Gmail account
*  using Arduino core for ESP8266 WiFi chip
*  by Boris Shobat
*  September 29 2016
*/
#ifndef G_SENDER
#define G_SENDER
#define GS_SERIAL_LOG_1         // Print to Serial only server response
#define GS_SERIAL_LOG_2       //  Print to Serial client commands and server response
#include <WiFiClientSecure.h>

#include "secrets.h"
#include "config.h"

class Gsender
{
    protected:
        Gsender();
    private:
        const int SMTP_PORT = SMTP_PORT_NUM;
        const char* SMTP_SERVER = SMTP_HOST;
        const char* EMAILBASE64_LOGIN = BASE64_LOGIN;
        const char* EMAILBASE64_PASSWORD = BASE64_PASSWORD;
        const char* FROM = FROM_ADDRESS;
        const char* _error = nullptr;
        char* _subject = nullptr;
        String _serverResponse;
        static Gsender* _instance;
        bool AwaitSMTPResponse(WiFiClientSecure &client, const String &resp = "", uint16_t timeOut = 10000);

    public:
        static Gsender* Instance();
        Gsender* Subject(const char* subject);
        Gsender* Subject(const String &subject);
        bool Send(const String &to, const String &message);
        String getLastResponse();
        const char* getError();
};
#endif // G_SENDER
