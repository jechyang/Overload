# R4 compatibility fixtures

legacy.ovscene preserves the first two actor nodes from the pre-R4
Examples/PerformanceLab/Project/Assets/Scenes/04_animation_A.ovscene.
No camera/light payload was rewritten. legacy.ovprefab wraps those same legacy
actor payloads in the existing PrefabOperations format, with the light parent
set to camera ID 1 to exercise hierarchy remapping. It is a legacy-format
fixture, not a claim of an independently supplied historical prefab file.

legacy-components.json freezes the original Inspector's 16 names/order and
ComponentTraits identities, plus Transform and the two abstract base identities.
The runtime test compares against this baseline rather than rebuilding expected
names from the new annotations. It additionally serializes and restores every
concrete native component using the real serializers, and tests opaque XML
through both Actor and Prefab saves.
