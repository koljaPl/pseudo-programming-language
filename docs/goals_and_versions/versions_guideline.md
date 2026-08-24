# Versioning

The language uses three numeric version components and an optional pre-release label:

```text
MAJOR.MINOR.PATCH[-STAGE]
```

Examples:

```text
1.0.0-alpha
1.2.0-beta
1.2.0
```

## Version Components

### Major

The **Major** version is increased when the language introduces significant or backward-incompatible changes.

Examples include:

* major syntax changes;
* removal or replacement of existing language features;
* major changes to the standard library;
* changes that may require existing programs to be updated.

### Minor

The **Minor** version is increased when new functionality is added without intentionally breaking existing code.

Examples include:

* new language features;
* new standard-library functionality;
* meaningful improvements to existing features.

Different development branches may target different Minor versions. Sub-branches may be created for more specific purposes.

### Patch

The **Patch** version is increased for small changes that do not introduce significant new functionality.

Examples include:

* bug fixes;
* internal improvements;
* minor performance optimizations;
* documentation corrections.

Patch changes may not always be listed in release announcements. They will still be recorded in the changelog or development reports.

## Release Stages

### Alpha

Alpha versions are intended primarily for development and testing.

They may be unstable, incomplete, or incompatible with previous versions. Alpha releases are not recommended for normal use.

Example:

```text
1.3.0-alpha
```

### Beta

Beta versions contain features that are mostly complete and are considered relatively stable.

They are suitable for users who want early access to upcoming functionality and are willing to encounter occasional bugs or breaking changes.

Example:

```text
1.3.0-beta
```

### Release

A version without a pre-release label is considered stable and recommended for general use.

Example:

```text
1.3.0
```

The word `release` is not normally included in the version number, because the absence of `alpha` or `beta` already indicates a stable release.
