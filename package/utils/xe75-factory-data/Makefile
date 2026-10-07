include $(TOPDIR)/rules.mk

PKG_NAME:=xe75-factory-data
PKG_RELEASE:=1
PKG_LICENSE:=GPL-2.0-or-later

include $(INCLUDE_DIR)/package.mk

define Package/xe75-factory-data
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Deco XE75 EU v3 read-only factory provisioning
  DEPENDS:=@TARGET_qualcommax_ipq50xx +ubi-utils
endef

define Build/Prepare
	$(INSTALL_DIR) $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_CPPFLAGS) -Wall -Wextra -Werror \
		-o $(PKG_BUILD_DIR)/xe75-factory-data $(PKG_BUILD_DIR)/xe75-factory-data.c $(TARGET_LDFLAGS)
endef

define Package/xe75-factory-data/install
	$(INSTALL_DIR) $(1)/usr/sbin $(1)/lib/preinit
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/xe75-factory-data $(1)/usr/sbin/
	$(INSTALL_DATA) ./files/08_xe75_factory_data $(1)/lib/preinit/
endef

$(eval $(call BuildPackage,xe75-factory-data))
