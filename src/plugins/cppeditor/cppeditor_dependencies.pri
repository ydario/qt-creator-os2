QTC_PLUGIN_NAME = CppEditor
os2:QTC_PLUGIN_NAME_SHORT = CppEdit
QTC_LIB_DEPENDS += \
    extensionsystem \
    utils \
    cplusplus
QTC_PLUGIN_DEPENDS += \
    texteditor \
    coreplugin \
    cpptools \
    projectexplorer
QTC_TEST_DEPENDS += \
    qmakeprojectmanager
