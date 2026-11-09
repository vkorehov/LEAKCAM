// the bench relay box's own app (skill leakcam-bench-relays): apart from the product app, so a relay switch never
// touches it (the product's test driver force-stopped the app at every switch, 2026-11-09)
pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}
dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}
rootProject.name = "leakcam-relay"
include(":app")
