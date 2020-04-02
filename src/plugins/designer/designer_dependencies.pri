QTC_PLUGIN_NAME = Designer
os2:QTC_PLUGIN_NAME_SHORT = Designr
QTC_LIB_DEPENDS += \
    cplusplus \
    extensionsystem \
    utils
QTC_PLUGIN_DEPENDS += \
    resourceeditor\
    cpptools \
    projectexplorer \
    qtsupport \
    texteditor \
    coreplugin
QTC_TEST_DEPENDS += \
    cppeditor
