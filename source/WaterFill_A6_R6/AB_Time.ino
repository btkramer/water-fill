// TAB - TIME
/***********************************************************
** Timekeeping functions for RTC and NTP (WebTime)        **
** Updates global 'timeData.current' for time and date     **
** Priority: RTC if enabled, else WebTime if enabled      **
** Periodically syncs RTC with WebTime if >1 min off      **
***********************************************************/

// Time-related variables
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -7 * 3600;
const int daylightOffset_sec = 0;

RTC_DS1307 rtc;
char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

struct TimeData {
    struct Current {
        int Hour = 0;
        int Minute = 0;
        int Second = 0;
        int Day = 1;
        int Month = 1;
        int Year = 1970;
        int DayOfWeek = 0;
        String Formatted = "";
    } current;

    struct AutoStart {
        int Hour = 10;
        int Minute = 0;
    } autoStart;

    struct Setpoint {
        int PumpOff = 10;
        int MaxPumpRun = 10;
        int OzoneRun = 60;
        int Debug = 10;
    } setpoint;
} timeData;

void setupTime() {
    // Always initialize WebTime for RTC correction, even if not enabled
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    int maxRetries = 5, retryCount = 0;
    time_t nowTime = 0;
    struct tm* timeinfo = nullptr;
    while ((nowTime = time(nullptr)) < 1000000000 && retryCount < maxRetries) {
        SerialPrint("Waiting for Web Time sync... (" + String(retryCount + 1) + "/" + String(maxRetries) + ")");
        delay(2000);
        retryCount++;
    }
    bool webTimeValid = (retryCount < maxRetries && nowTime >= 1000000000);
    if (webTimeValid) {
        timeinfo = localtime(&nowTime);
        if (timeinfo->tm_year + 1900 < 2020) webTimeValid = false; // Validate year
    }
    if (webTimeValid) {
        SerialPrint("Web Time synchronized: " + String(timeinfo->tm_year + 1900) + "-" +
                    String(timeinfo->tm_mon + 1) + "-" + String(timeinfo->tm_mday) + " " +
                    String(timeinfo->tm_hour) + ":" + 
                    String((timeinfo->tm_min < 10 ? "0" : "") + String(timeinfo->tm_min)) + ":" +
                    String((timeinfo->tm_sec < 10 ? "0" : "") + String(timeinfo->tm_sec)));
    } else {
        SerialPrint("Web Time sync failed after " + String(maxRetries) + " attempts. Using compile time for RTC.");
    }

    if (enabled.RTC) {
        retryCount = 0;
        while (!rtc.begin() && retryCount < maxRetries) {
            SerialPrint("RTC init failed, retrying... (" + String(retryCount + 1) + "/" + String(maxRetries) + ")");
            retryCount++;
            delay(1000);
        }
        if (retryCount == maxRetries) {
            SerialPrint("RTC init failed after " + String(maxRetries) + " retries.");
            return;
        }
        DateTime rtcTime = rtc.now();
        SerialPrint("RTC time before adjustment: " + String(rtcTime.year()) + "-" +
                    String(rtcTime.month()) + "-" + String(rtcTime.day()) + " " +
                    String(rtcTime.hour()) + ":" + 
                    String((rtcTime.minute() < 10 ? "0" : "") + String(rtcTime.minute())) + ":" +
                    String((rtcTime.second() < 10 ? "0" : "") + String(rtcTime.second())));
        bool adjustRTC = !rtc.isrunning() || rtcTime.year() < 2020; // Adjust if not running or invalid year
        if (webTimeValid && !adjustRTC) {
            long rtcSeconds = rtcTime.hour() * 3600 + rtcTime.minute() * 60 + rtcTime.second();
            long webSeconds = timeinfo->tm_hour * 3600 + timeinfo->tm_min * 60 + timeinfo->tm_sec;
            if (abs(rtcSeconds - webSeconds) > 60) adjustRTC = true; // Adjust if >1 min off
        }
        if (adjustRTC) {
            if (webTimeValid) {
                SerialPrint("RTC not running, invalid, or off by >1 min, setting to Web Time.");
                rtc.adjust(DateTime(timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
                                    timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec));
            } else {
                SerialPrint("RTC not running or invalid, setting to compile time.");
                rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
            }
        }
        rtcTime = rtc.now();
        SerialPrint("RTC initialized: " + String(rtcTime.year()) + "-" +
                    String(rtcTime.month()) + "-" + String(rtcTime.day()) + " " +
                    String(rtcTime.hour()) + ":" + 
                    String((rtcTime.minute() < 10 ? "0" : "") + String(rtcTime.minute())) + ":" +
                    String((rtcTime.second() < 10 ? "0" : "") + String(rtcTime.second())));
    }
    updateTime();
}

void loopTime() {
    if (!enabled.RTC && !enabled.WebTime) return;
    updateTime();

    // Periodically check WebTime to correct RTC
    static unsigned long lastSync = 0;
    if (millis() - lastSync > 3600000) { // Every hour
        time_t nowTime = time(nullptr);
        if (nowTime > 1000000000 && enabled.RTC) {
            struct tm* timeinfo = localtime(&nowTime);
            if (timeinfo->tm_year + 1900 >= 2020) { // Validate year
                DateTime rtcTime = rtc.now();
                long rtcSeconds = rtcTime.hour() * 3600 + rtcTime.minute() * 60 + rtcTime.second();
                long webSeconds = timeinfo->tm_hour * 3600 + timeinfo->tm_min * 60 + timeinfo->tm_sec;
                if (abs(rtcSeconds - webSeconds) > 60) { // More than 1 minute difference
                    SerialPrint("RTC time off by " + String(abs(rtcSeconds - webSeconds)) + " seconds, syncing to Web Time.");
                    rtc.adjust(DateTime(timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
                                        timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec));
                }
            }
        }
        if (enabled.WebTime) {
            configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
        }
        lastSync = millis();
    }
}

void updateTime() {
    if (enabled.RTC) {
        DateTime now = rtc.now();
        timeData.current.Hour = now.hour();
        timeData.current.Minute = now.minute();
        timeData.current.Second = now.second();
        timeData.current.Day = now.day();
        timeData.current.Month = now.month();
        timeData.current.Year = now.year();
        timeData.current.DayOfWeek = now.dayOfTheWeek();
    } else if (enabled.WebTime) {
        time_t nowTime = time(nullptr);
        struct tm* timeinfo = localtime(&nowTime);
        timeData.current.Hour = timeinfo->tm_hour;
        timeData.current.Minute = timeinfo->tm_min;
        timeData.current.Second = timeinfo->tm_sec;
        timeData.current.Day = timeinfo->tm_mday;
        timeData.current.Month = timeinfo->tm_mon + 1;
        timeData.current.Year = timeinfo->tm_year + 1900;
        timeData.current.DayOfWeek = timeinfo->tm_wday;
    }
    timeData.current.Formatted = formatTime();
}

String formatTime() {
    int hour12 = timeData.current.Hour;
    String amPm = "AM";
    if (hour12 == 0) hour12 = 12;
    else if (hour12 >= 12) {
        amPm = "PM";
        if (hour12 > 12) hour12 -= 12;
    }
    return String(daysOfTheWeek[timeData.current.DayOfWeek]) + ", " +
           String(timeData.current.Month) + "/" +
           String((timeData.current.Day < 10 ? "0" : "") + String(timeData.current.Day)) + "/" +
           String(timeData.current.Year) + " " +
           String(hour12) + ":" +
           String((timeData.current.Minute < 10 ? "0" : "") + String(timeData.current.Minute)) + ":" +
           String((timeData.current.Second < 10 ? "0" : "") + String(timeData.current.Second)) + " " + amPm;
}

void printTime(const char* source) {
    String timeString = String(source) + " Time: " + timeData.current.Formatted;
    SerialPrint(timeString);
}