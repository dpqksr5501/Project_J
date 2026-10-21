# Project J: matching Mover source dependency

Pristine Epic UE 5.8.3 (`58210709`) source/config/resources/descriptor from `Engine/Plugins/Experimental/Mover`. No Mover source logic is patched here. Copyright headers are retained.

This project copy is necessary because Mover depends on the project's patched NetworkPrediction plugin; UBT does not allow an installed engine module to depend on a project module. The project copies are the active implementations. See [NetworkPrediction patch](../NetworkPrediction/PROJECT_J_PATCH.md) for scope, validation and the paired removal/upgrade contract.
