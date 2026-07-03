#ifndef WEB_SERVER_H
#define WEB_SERVER_H
// #include <WebServer.h>
#include <ESP8266WebServer.h>
void start_webserver();
void handleClient();
void updateSimulatedWaterLevel();

#endif // WEB_SERVER_H

extern unsigned long lastUpdate;  // Declare once here
