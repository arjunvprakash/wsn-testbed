#ifndef COMMON_H
#define COMMON_H

#define ADDR_Gateway '\x00'

#define CTRL_BME680 '\x00'
#define CTRL_AM2302 '\x01'
#define CTRL_SR04 '\x02'
#define CTRL_ANW '\x03'
#define CTRL_CO2 '\x04'

// Kontrollflags
#define CTRL_RET '\xC1' // Antwort des Moduls
#define CTRL_MSG '\xC4' // Nachricht
#define CTRL_ACK '\xC5' // Acknowledgement
#define CTRL_PKT '\x45' // Routing layer packet
#define CTRL_BCN '\x47' // Routing layer beacon

#endif
