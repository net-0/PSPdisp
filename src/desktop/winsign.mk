# This file uses the WINSIGN_* prefix, as it's included by the Makefile of the desktop (on Windows only), which gives it BIN_DIR
#
# The self-signed code signing certificate of the desktop (Windows), that signs its driver packages (e.g. the catalog of the WinUSB one, see daemon/winusb.mk), for the users to trust.
# The files that sign with it are included after this one, so they have WINSIGN_DIR for their prerequisites.

# The folder of the certificate
WINSIGN_DIR := $(BIN_DIR)/winsign

# The password of the .pfx of the certificate, given to openssl and signtool
WINSIGN_PFX_PASSWORD := leeeroyjenkins

ALL += $(WINSIGN_DIR)/pspdisp-winsign.pfx $(WINSIGN_DIR)/pspdisp-winsign.cer

# The folder of the certificate, created before it's made in it (an order-only prerequisite)
$(WINSIGN_DIR):
	mkdir -p $@

# The certificate, made once and kept by the next runs (delete it for a new one): its key with the certificate (pspdisp-winsign.pfx, of password WINSIGN_PFX_PASSWORD, for signtool),
# and the certificate alone (pspdisp-winsign.cer, for the users to trust)
# (the openssl of UCRT64 is a Windows program, so MSYS2 would turn the /CN= of the subject into a path)
$(WINSIGN_DIR)/pspdisp-winsign.pfx $(WINSIGN_DIR)/pspdisp-winsign.cer &: | $(WINSIGN_DIR)
	MSYS2_ARG_CONV_EXCL='/CN=' openssl req \
		-quiet \
		-x509 \
		-newkey rsa:3072 \
		-sha256 \
		-days 3650 \
		-noenc \
		-subj "/CN=PSPdisp" \
		-addext "keyUsage=critical,digitalSignature" \
		-addext "extendedKeyUsage=codeSigning" \
		-addext "basicConstraints=critical,CA:FALSE" \
		-keyout $(WINSIGN_DIR)/pspdisp-winsign.key \
		-out $(WINSIGN_DIR)/pspdisp-winsign.crt
	openssl pkcs12 -export -inkey $(WINSIGN_DIR)/pspdisp-winsign.key -in $(WINSIGN_DIR)/pspdisp-winsign.crt -out $(WINSIGN_DIR)/pspdisp-winsign.pfx -passout pass:$(WINSIGN_PFX_PASSWORD)
	openssl x509 -in $(WINSIGN_DIR)/pspdisp-winsign.crt -outform der -out $(WINSIGN_DIR)/pspdisp-winsign.cer
	rm -f $(WINSIGN_DIR)/pspdisp-winsign.key $(WINSIGN_DIR)/pspdisp-winsign.crt
