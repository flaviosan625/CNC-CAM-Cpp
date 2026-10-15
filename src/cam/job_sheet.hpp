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
    std::string material_type;
    std::string area_m2;
};

std::string bridge_summary(const ToolpathJob* job);

// Placement of the drawings on the landscape sheet, in the PDF writer's device
// pixels.  The sizes come from the rendered previews so nothing is stretched.
struct JobSheetDrawing {
    int summary_width = 880;
    int summary_height = 269;
    int operation_width = 1540;
    int operation_height = 471;
    // Legacy preview count. The OS writer never appends per-toolpath sheets.
    int operation_sheets = 0;
};

std::string job_setup_sheet_html(const std::vector<SetupSheetOperation>& operations,
                                const cad::MaterialSetup& material,
                                const std::string& job_name,
                                const SetupSheetInfo& info,
                                const JobSheetDrawing& drawing = {});

bool write_job_setup_sheet_pdf(const QString& path,
                               const std::vector<SetupSheetOperation>& operations,
                               const cad::MaterialSetup& material,
                               const std::string& job_name,
                               const SetupSheetInfo& info,
                               const QImage& layout_preview,
                               const QImage& logo,
                               const std::vector<QImage>& operation_previews,
                               QString* error = nullptr);

struct MultiJobSheetEntry {
    std::vector<SetupSheetOperation> operations;
    cad::MaterialSetup material;
    std::string job_name;
    SetupSheetInfo info;
    QImage layout_preview;
    std::vector<QImage> operation_previews;
};

bool write_multi_job_setup_sheet_pdf(const QString& path,
                                     const std::vector<MultiJobSheetEntry>& entries,
                                     const QImage& logo,
                                     QString* error = nullptr);

}  // namespace cnc::cam
