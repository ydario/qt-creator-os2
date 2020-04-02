QTC_PLUGIN_NAME = Debugger
os2:QTC_PLUGIN_NAME_SHORT = Debugr
QTC_LIB_DEPENDS += \
    aggregation \
    cplusplus \
    extensionsystem \
    languageutils \
    utils \
    qmldebug \
    qmljs \
    ssh
QTC_PLUGIN_DEPENDS += \
    coreplugin \
    cpptools \
    projectexplorer \
    qtsupport \
    texteditor
QTC_PLUGIN_RECOMMENDS += \
    cppeditor \
    bineditor
QTC_TEST_DEPENDS += \
    qmakeprojectmanager

