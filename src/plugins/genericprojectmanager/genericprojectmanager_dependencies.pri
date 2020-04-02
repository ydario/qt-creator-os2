QTC_PLUGIN_NAME = GenericProjectManager
os2:QTC_PLUGIN_NAME_SHORT = GenPMgr
QTC_LIB_DEPENDS += \
    extensionsystem \
    utils
QTC_PLUGIN_DEPENDS += \
    coreplugin \
    projectexplorer \
    texteditor \
    qtsupport
QTC_PLUGIN_RECOMMENDS += \
    cpptools
QTC_TEST_DEPENDS += \
    cppeditor
