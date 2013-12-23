top = '.'
out = 'build'

def options(ctx):
    ctx.load('pebble_sdk')

def configure(ctx):
    ctx.load('pebble_sdk')

def build(ctx):
    ctx.load('pebble_sdk')

    # libfixmath configuration:
    #   FIXMATH_NO_OVERFLOW: Inlines some operations.
    #   FIXMATH_NO_CACHE: Removes large caches for slow computations.

    ctx.pbl_program(source=ctx.path.ant_glob('src/**/*.c'),
                    target='pebble-app.elf',
                    cflags=['-Werror', '-DFIXMATH_NO_OVERFLOW', '-DFIXMATH_NO_CACHE'])

    ctx.pbl_bundle(elf='pebble-app.elf',
                   js=ctx.path.ant_glob('src/js/**/*.js'))
