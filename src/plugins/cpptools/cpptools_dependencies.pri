QTC_PLUGIN_NAME = CppTools
os2:QTC_PLUGIN_NAME_SHORT = CppTool
QTC_LIB_DEPENDS += \
    cplusplus \
    extensionsystem \
    utils
QTC_PLUGIN_DEPENDS += \
    coreplugin \
    projectexplorer \
    texteditor
QTC_TEST_DEPENDS += \
    cppeditor \
    qmakeprojectmanager
