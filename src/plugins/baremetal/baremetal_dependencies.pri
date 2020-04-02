QTC_PLUGIN_NAME = BareMetal
os2:QTC_PLUGIN_NAME_SHORT = BrMtl
QTC_LIB_DEPENDS += \
    extensionsystem \
    ssh \
    utils
QTC_PLUGIN_DEPENDS += \
    coreplugin \
    debugger \
    projectexplorer

QTC_PLUGIN_RECOMMENDS += \
    # optional plugin dependencies. nothing here at this time

