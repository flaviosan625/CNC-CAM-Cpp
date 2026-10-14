#pragma once

#include <string>
#include <vector>

#include <QImage>
#include <QString>

#include "cad/material.hpp"
#include "cam/job.hpp"

namespace cnc::cam {

struct SetupSheetOperation {
    const Toolpath* path = nullptr;
    const ToolpathJob* job = nullptr;
};

struct SetupSheetInfo {
    std::string customer;
    std::string contact;
    std::string order_number;
    std::string part_name;
    std::string revision;
    std::string operator_name;
    std::string notes;
    std::string logo_source;
};

std::string job_setup_sheet_html(const std::vector<SetupSheetOperation>& operations,
                                const cad::MaterialSetup& material,
                                const std::string& job_name,
                                const SetupSheetInfo& info);

bool write_job_setup_sheet_pdf(const QString& path,
                               const std::vector<SetupSheetOperation>& operations,
                               const cad::MaterialSetup& material,
                               const std::string& job_name,
                               const SetupSheetInfo& info,
                               const QImage& layout_preview,
                               const QImage& logo,
                               QString* error = nullptr);

}  // namespace cnc::cam
