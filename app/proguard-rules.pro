# Keep JNI entry points for native libraries (loaded reflectively by name).
-keepclasseswithmembernames class dev.itzkaguya.aospcontainer.core.** {
    native <methods>;
}
