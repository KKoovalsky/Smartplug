PROGRAM=Smartplug
PROGRAM_SRC_DIR=/home/kacper/Workspace/ESP8266/Smartplug

EXTRA_CFLAGS=-DLWIP_HTTPD_CGI=1 -DLWIP_HTTPD_SSI=1 -I./fsdata
EXTRA_COMPONENTS=extras/mbedtls extras/httpd

#html:
#		@echo "Generating fsdata.."
#		cd fsdata && ./makefsdata

include /home/kacper/ESPOpenRTOS/common.mk