QTC_PLUGIN_NAME = CMakeProjectManager
os2:QTC_PLUGIN_NAME_SHORT = CMkPMgr
QTC_LIB_DEPENDS += \
    extensionsystem \
    qmljs \
    utils
QTC_PLUGIN_DEPENDS += \
    coreplugin \
    projectexplorer \
    cpptools \
    texteditor \
    qtsupport
QTC_PLUGIN_RECOMMENDS += \
    designer
