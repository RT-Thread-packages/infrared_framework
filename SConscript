from building import *

cwd = GetCurrentDir()
src = []
path = []

# A single top-level Kconfig choice selects exactly one independent code path.
if GetDepend(['INFRARED_LEGACY']):
    src = ['src/infrared.c']
    if GetDepend(['INFRARED_NEC_DECODER']):
        src += ['src/nec_decoder.c']
    if GetDepend(['PKG_USING_DRV_INFRARED']):
        src += ['src/drv_infrared.c']
    path = [cwd + '/inc', cwd + '/src']

elif GetDepend(['INFRARED_FRAMEWORK']):
    src = ['framework/src/infrared_device.c', 'framework/src/infrared_callback.c']
    if GetDepend(['INFRARED_DEVICE_PIN']):
        src += ['framework/src/backends/input/infrared_pin.c']
    if GetDepend(['INFRARED_USING_INPUT_CAPTURE']):
        src += ['framework/src/backends/input/infrared_capture.c']
    if GetDepend(['INFRARED_DEVICE_PWM']):
        src += ['framework/src/backends/output/infrared_pwm.c']
    if GetDepend(['INFRARED_USING_PULSE_DISTANCE']):
        src += ['framework/codecs/pulse_distance/pulse_distance.c']
    if GetDepend(['INFRARED_FRAMEWORK_NEC_CODEC']):
        src += ['framework/codecs/nec/nec_codec.c']
    if GetDepend(['INFRARED_USING_EXAMPLE']):
        src += ['framework/examples/infrared_example.c']
    if GetDepend(['INFRARED_USING_MSH']):
        src += ['framework/examples/infrared_msh.c']
        if GetDepend(['INFRARED_FRAMEWORK_NEC_CODEC']):
            src += ['framework/codecs/nec/nec_msh.c']
    path = [cwd + '/framework/inc', cwd + '/framework/src',
            cwd + '/framework/src/backends/input',
            cwd + '/framework/src/backends/output',
            cwd + '/framework/codecs/nec',
            cwd + '/framework/codecs/pulse_distance']
    if GetDepend(['INFRARED_USING_EXAMPLE']):
        path += [cwd + '/framework/examples']

group = DefineGroup('Infrared_frame', src, depend=['PKG_USING_INFRARED'], CPPPATH=path)
Return('group')
