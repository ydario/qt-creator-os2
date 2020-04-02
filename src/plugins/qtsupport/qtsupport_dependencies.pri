QTC_PLUGIN_NAME = QtSupport
os2:QTC_PLUGIN_NAME_SHORT = QtSupp
QTC_LIB_DEPENDS += \
    extensionsystem \
    utils
QTC_PLUGIN_DEPENDS += \
    coreplugin \
    projectexplorer \
    resourceeditor
DEFINES *= \
    QMAKE_AS_LIBRARY \
    PROPARSER_THREAD_SAFE \
    PROEVALUATOR_THREAD_SAFE \
    PROEVALUATOR_CUMULATIVE \
    PROEVALUATOR_DUAL_VFS \
    PROEVALUATOR_SETENV
INCLUDEPATH *= $$PWD/../../shared
