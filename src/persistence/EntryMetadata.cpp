#include "EntryMetadata.h"
#include <sstream>
#include <algorithm>

namespace Persistence {

std::string EntryMetadata::ToCommentLine() const
{
    // Format: ; EditorId|DisplayName|MeshPath|FormType
    // Empty fields are preserved as empty strings between pipes
    std::ostringstream ss;
    ss << "; " << editorId << "|" << displayName << "|" << meshName << "|" << formTypeName;
    return ss.str();
}

bool EntryMetadata::ParseFromComment(std::string_view commentLine, EntryMetadata& outMetadata)
{
    // Must start with "; "
    if (commentLine.size() < 2 || commentLine[0] != ';') {
        return false;
    }

    // Skip "; " prefix
    std::string_view content = commentLine.substr(1);

    // Trim leading whitespace
    while (!content.empty() && (content[0] == ' ' || content[0] == '\t')) {
        content = content.substr(1);
    }

    // Must contain at least two pipes (3+ fields)
    size_t pipeCount = std::count(content.begin(), content.end(), '|');
    if (pipeCount < 2) {
        return false;
    }

    // Split by '|'
    std::vector<std::string> fields;
    std::string contentStr(content);
    std::istringstream ss(contentStr);
    std::string field;

    while (std::getline(ss, field, '|')) {
        // Trim whitespace from field
        size_t start = field.find_first_not_of(" \t");
        size_t end = field.find_last_not_of(" \t");
        if (start != std::string::npos && end != std::string::npos) {
            field = field.substr(start, end - start + 1);
        } else {
            field.clear();
        }
        fields.push_back(field);
    }

    // Need at least 3 fields (editorId, displayName, meshName)
    if (fields.size() < 3) {
        return false;
    }

    outMetadata.editorId = fields[0];
    outMetadata.displayName = fields[1];
    outMetadata.meshName = fields[2];

    // FormType is optional (4th field) - supports both old 3-field and new 4-field format
    if (fields.size() >= 4) {
        outMetadata.formTypeName = fields[3];
    }

    return true;
}

bool EntryMetadata::IsEmpty() const
{
    // FormType doesn't count as "real" metadata - it's a fallback identifier
    return editorId.empty() && displayName.empty() && meshName.empty();
}

bool EntryMetadata::IsCompletelyEmpty() const
{
    return editorId.empty() && displayName.empty() && meshName.empty() && formTypeName.empty();
}

void EntryMetadata::MergeFrom(const EntryMetadata& other)
{
    // Only fill in empty fields from other
    if (editorId.empty() && !other.editorId.empty()) {
        editorId = other.editorId;
    }
    if (displayName.empty() && !other.displayName.empty()) {
        displayName = other.displayName;
    }
    if (meshName.empty() && !other.meshName.empty()) {
        meshName = other.meshName;
    }
    if (formTypeName.empty() && !other.formTypeName.empty()) {
        formTypeName = other.formTypeName;
    }
}

std::string ExtractPluginFromFormKey(const std::string& formKeyString)
{
    size_t tildePos = formKeyString.find('~');
    if (tildePos != std::string::npos && tildePos + 1 < formKeyString.size()) {
        return formKeyString.substr(tildePos + 1);
    }
    return "";
}

std::set<std::string> CollectPluginNames(const std::vector<std::string>& formKeyStrings)
{
    std::set<std::string> plugins;
    for (const auto& formKey : formKeyStrings) {
        std::string plugin = ExtractPluginFromFormKey(formKey);
        if (!plugin.empty()) {
            plugins.insert(plugin);
        }
    }
    return plugins;
}

} // namespace Persistence
