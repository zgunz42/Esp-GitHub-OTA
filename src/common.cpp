#ifdef ESP8266
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#elif defined(ESP32)
#include <WiFiClientSecure.h>
#include <Update.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#endif

#include <ArduinoJson.h>
#include "common.h"
#include "semver.h"
#include "semver_extensions.h"

String get_updated_base_url_via_api(WiFiClientSecure wifi_client, String release_url)
{
  const char *TAG = "get_updated_base_url_via_api";
  ESP_LOGI(TAG, "Release_url: %s\n", release_url.c_str());

  HTTPClient https;
  String base_url = "";

#ifdef ESP8266
  bool mfln = wifi_client.probeMaxFragmentLength("github.com", 443, 1024);
  ESP_LOGI(TAG, "MFLN supported: %s\n", mfln ? "yes" : "no");
  if (mfln) { wifi_client.setBufferSizes(1024, 1024); }
#endif

  if (!https.begin(wifi_client, release_url))
  {
    ESP_LOGI(TAG, "[HTTPS] Unable to connect\n");
    return base_url;
  }

  int httpCode = https.GET();
  if (httpCode < 0 || httpCode >= 400)
  {
    ESP_LOGI(TAG, "[HTTPS] GET... failed, error: %s\n", https.errorToString(httpCode).c_str());
    char errorText[128];
#ifdef ESP8266
    int errCode = wifi_client.getLastSSLError(errorText, sizeof(errorText));
#elif defined(ESP32)
    int errCode = wifi_client.lastError(errorText, sizeof(errorText));
#endif
    ESP_LOGV(TAG, "httpCode: %d, errorCode %d: %s\n", httpCode, errCode, errorText);
  }
  else if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY)
  {
    StaticJsonDocument<64> filter;
    filter["html_url"] = true;

    StaticJsonDocument<256> doc;
    auto result = deserializeJson(doc, https.getStream(), DeserializationOption::Filter(filter));
    if (result != DeserializationError::Ok) {
      ESP_LOGI(TAG, "deserializeJson error %s\n", result.c_str());
    }

    base_url = String((const char *)doc["html_url"]);
    base_url.replace("tag", "download");
    base_url += "/";
  }

  https.end();
  return base_url;
}

String get_updated_base_url_via_redirect(WiFiClientSecure wifi_client, String release_url)
{
  const char *TAG = "get_updated_base_url_via_redirect";

  String location = get_redirect_location(wifi_client, release_url);
  ESP_LOGV(TAG, "location: %s\n", location.c_str());

  if (location.length() <= 0)
  {
    ESP_LOGE(TAG, "[HTTPS] No redirect url\n");
    return "";
  }

  String base_url = "";
  base_url = location + "/";
  base_url.replace("tag", "download");

  ESP_LOGV(TAG, "returns: %s\n", base_url.c_str());
  return base_url;
}

String get_redirect_location(WiFiClientSecure wifi_client, String initial_url)
{
  const char *TAG = "get_redirect_location";
  ESP_LOGV(TAG, "initial_url: %s\n", initial_url.c_str());

  HTTPClient https;
  https.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);

#ifdef ESP8266
  bool mfln = wifi_client.probeMaxFragmentLength("github.com", 443, 1024);
  ESP_LOGI(TAG, "MFLN supported: %s\n", mfln ? "yes" : "no");
  if (mfln) { wifi_client.setBufferSizes(1024, 1024); }
#endif

  if (!https.begin(wifi_client, initial_url))
  {
    ESP_LOGE(TAG, "[HTTPS] Unable to connect\n");
    return "";
  }

  int httpCode = https.GET();
  if (httpCode != HTTP_CODE_FOUND)
  {
    ESP_LOGE(TAG, "[HTTPS] GET... failed, No redirect\n");
    char errorText[128];
#ifdef ESP8266
    int errCode = wifi_client.getLastSSLError(errorText, sizeof(errorText));
#elif defined(ESP32)
    int errCode = wifi_client.lastError(errorText, sizeof(errorText));
#endif
    ESP_LOGV(TAG, "httpCode: %d, errorCode %d: %s\n", httpCode, errCode, errorText);
  }

  String redirect_url = https.getLocation();
  https.end();

  ESP_LOGV(TAG, "returns: %s\n", redirect_url.c_str());
  return redirect_url;
}

void print_update_result(Updater updater, HTTPUpdateResult result, const char *TAG)
{
  switch (result){
    case HTTP_UPDATE_FAILED:
      ESP_LOGI(TAG, "HTTP_UPDATE_FAILED Error (%d): %s\n", updater.getLastError(), updater.getLastErrorString().c_str());
      break;
    case HTTP_UPDATE_NO_UPDATES:
      ESP_LOGI(TAG, "HTTP_UPDATE_NO_UPDATES\n");
      break;
    case HTTP_UPDATE_OK:
      ESP_LOGI(TAG, "HTTP_UPDATE_OK\n");
      break;
  }
}

bool update_required(semver_t _new_version, semver_t _current_version){
  return _new_version > _current_version;
}

void update_started()
{
  ESP_LOGI("update_started", "HTTP update process started\n");
}

void update_finished()
{
  ESP_LOGI("update_finished", "HTTP update process finished\n");
}

void update_progress(int currentlyReceiced, int totalBytes)
{
  ESP_LOGI("update_progress", "Data received, Progress: %.2f %%\r", 100.0 * currentlyReceiced / totalBytes);
}

void update_error(int err)
{
  ESP_LOGI("update_error", "HTTP update fatal error code %d\n", err);
}

// Set time via NTP, as required for x.509 validation
void synchronize_system_time()
{
  configTime(3 * 3600, 0, "pool.ntp.org", "time.nist.gov");

  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2)
  {
    delay(100);
    now = time(nullptr);
  }
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
}


const char *github_certificate PROGMEM = R"CERT(
-----BEGIN CERTIFICATE-----
MIIEojCCBEigAwIBAgIRAIvcD/9Udy+q0XMnPyM2Kq8wCgYIKoZIzj0EAwIwgY8x
CzAJBgNVBAYTAkdCMRswGQYDVQQIExJHcmVhdGVyIE1hbmNoZXN0ZXIxEDAOBgNV
BAcTB1NhbGZvcmQxGDAWBgNVBAoTD1NlY3RpZ28gTGltaXRlZDE3MDUGA1UEAxMu
U2VjdGlnbyBFQ0MgRG9tYWluIFZhbGlkYXRpb24gU2VjdXJlIFNlcnZlciBDQTAe
Fw0yNDAzMDcwMDAwMDBaFw0yNTAzMDcyMzU5NTlaMBcxFTATBgNVBAMMDCouZ2l0
aHViLmNvbTBZMBMGByqGSM49AgEGCCqGSM49AwEHA0IABHADGElEmwEKQTOjCTeZ
EQ+YFacbykIKQ+I0OI2NQqjTnlj+3zpJ/j8XYiau+kL+Wz5r97U8Q+qZYaDQ2A6I
bzKjggL6MIIC9jAfBgNVHSMEGDAWgBT2hQo7EYbhBH0Oqgss0u7MZHt7rjAdBgNV
HQ4EFgQULNWfMkiYavm5W71lUenpddcgsZYwDgYDVR0PAQH/BAQDAgeAMAwGA1Ud
EwEB/wQCMAAwHQYDVR0lBBYwFAYIKwYBBQUHAwEGCCsGAQUFBwMCMEkGA1UdIARC
MEAwNAYLKwYBBAGyMQECAgcwJTAjBggrBgEFBQcCARYXaHR0cHM6Ly9zZWN0aWdv
LmNvbS9DUFMwCAYGZ4EMAQIBMIGEBggrBgEFBQcBAQR4MHYwTwYIKwYBBQUHMAKG
Q2h0dHA6Ly9jcnQuc2VjdGlnby5jb20vU2VjdGlnb0VDQ0RvbWFpblZhbGlkYXRp
b25TZWN1cmVTZXJ2ZXJDQS5jcnQwIwYIKwYBBQUHMAGGF2h0dHA6Ly9vY3NwLnNl
Y3RpZ28uY29tMIIBfgYKKwYBBAHWeQIEAgSCAW4EggFqAWgAdQDPEVbu1S58r/OH
W9lpLpvpGnFnSrAX7KwB0lt3zsw7CAAAAY4WRSJBAAAEAwBGMEQCIEn9RPTj/mWa
DNJYWLd5adwMh7lQLd0H4U687S0Vgw+IAiAWGZTh1I6KUlgK6RI2mF1Q+xxZvOIg
8FYfjyZYiosoewB3AKLjCuRF772tm3447Udnd1PXgluElNcrXhssxLlQpEfnAAAB
jhZFIbkAAAQDAEgwRgIhALcwnByZOMSyk9PPja2cXaA5/xwHG3n9zTUeVnErWwcS
AiEA5+cybN+U4jFHP/uWSLhtrysqmW+SUJnFxS2HNgjr25QAdgBOdaMnXJoQwzhb
bNTfP1LrHfDgjhuNacCx+mSxYpo53wAAAY4WRSG4AAAEAwBHMEUCIQD630D/OUY5
I0wDRYh6rvYhIaFYSHArqdPkP7vtRZa4EgIgOVY+ffcboNA/3MVjV4HbPJcFPntP
uh2wvdD4mlcFFkEwIwYDVR0RBBwwGoIMKi5naXRodWIuY29tggpnaXRodWIuY29t
MAoGCCqGSM49BAMCA0gAMEUCIQC4ntULeA2fj3hbPy2Ho1aUiJLU/qZe568a8//X
5PNAOAIgbK6eh8zyKr7uYc1/1qRMeDrvekcpOGOFnOHd3aOXVqc=
-----END CERTIFICATE-----
)CERT";
