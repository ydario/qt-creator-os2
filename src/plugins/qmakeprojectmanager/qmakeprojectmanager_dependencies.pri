QTC_PLUGIN_NAME = QmakeProjectManager
os2:QTC_PLUGIN_NAME_SHORT = QmkPMgr
QTC_LIB_DEPENDS += \
    extensionsystem \
    qmljs \
    utils
QTC_PLUGIN_DEPENDS += \
    coreplugin \
    projectexplorer \
    qtsupport \
    texteditor \
    cpptools \
    resourceeditor

QTC_PLUGIN_RECOMMENDS += \
    designer
