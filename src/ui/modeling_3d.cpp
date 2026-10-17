#include "ui/modeling_3d.hpp"

#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "viewport/gl_widget.hpp"

namespace cnc::ui {
using namespace cad::model3d;

Modeling3DWorkspace::Modeling3DWorkspace(QWidget* parent) : QWidget(parent) {
    setObjectName("Modelagem3DWorkspace");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 8, 10, 8);
    root->setSpacing(8);
    auto* top = new QHBoxLayout;
    auto* title = new QLabel("MODELAGEM 3D · PEÇA PARAMÉTRICA", this);
    title->setObjectName("SectionHeader");
    top->addWidget(title, 1);
    for (const auto& [label, callback] : std::vector<std::pair<QString, std::function<void()>>>{
             {"Nova peça", [this]{new_part();}}, {"Abrir peça", [this]{open_part();}},
             {"Salvar", [this]{save_part();}}, {"Salvar como", [this]{save_part_as();}}}) {
        auto* b = new QPushButton(label, this); top->addWidget(b);
        connect(b, &QPushButton::clicked, this, callback);
    }
    root->addLayout(top);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    auto* left = new QWidget(splitter);
    left->setMinimumWidth(270);
    auto* left_layout = new QVBoxLayout(left);
    left_layout->setContentsMargins(0,0,4,0);
    auto* tree_title = new QLabel("ÁRVORE DA PEÇA", left); tree_title->setObjectName("SectionHeader");
    left_layout->addWidget(tree_title);
    tree_ = new QTreeWidget(left); tree_->setHeaderHidden(true); tree_->setObjectName("Model3DFeatureTree");
    tree_->setMinimumHeight(160); left_layout->addWidget(tree_,1);
    connect(tree_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
        select_feature(current ? current->data(0, Qt::UserRole).toInt() : -1);
    });
    auto* property_title = new QLabel("ESBOÇO E EXTRUSÃO", left); property_title->setObjectName("SectionHeader");
    left_layout->addWidget(property_title);
    auto* form = new QFormLayout; form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    profile_ = new QComboBox(left); profile_->addItem("Retângulo", int(Profile::Rectangle)); profile_->addItem("Círculo", int(Profile::Circle)); profile_->addItem("Triângulo", int(Profile::Triangle)); profile_->addItem("Hexágono", int(Profile::Hexagon));
    form->addRow("Perfil do esboço", profile_);
    auto make_num = [left](double lo,double hi,double value,int decimals=2){auto* s=new QDoubleSpinBox(left);s->setRange(lo,hi);s->setDecimals(decimals);s->setValue(value);s->setSuffix(" mm");return s;};
    width_=make_num(.1,100000,100); height_=make_num(.1,100000,60); start_z_=make_num(-100000,100000,0); depth_=make_num(.1,100000,18); depth2_=make_num(0,100000,18); target_z_=make_num(-100000,100000,18); offset_=make_num(-100000,100000,0); draft_=make_num(-89,89,0,2); draft_->setSuffix(" °");
    form->addRow("Largura / diâmetro",width_); form->addRow("Altura",height_); form->addRow("Plano do esboço Z",start_z_);
    end_=new QComboBox(left);
    end_->addItem("Cego",int(EndCondition::Blind)); end_->addItem("Atravessar tudo",int(EndCondition::ThroughAll)); end_->addItem("Atravessar tudo · ambos os lados",int(EndCondition::ThroughAllBoth)); end_->addItem("Plano médio",int(EndCondition::MidPlane)); end_->addItem("Até próximo",int(EndCondition::UpToNext)); end_->addItem("Até vértice",int(EndCondition::UpToVertex)); end_->addItem("Até superfície",int(EndCondition::UpToSurface)); end_->addItem("Offset da superfície",int(EndCondition::OffsetFromSurface)); end_->addItem("Até corpo",int(EndCondition::UpToBody));
    form->addRow("Condição final",end_); form->addRow("Profundidade 1",depth_); form->addRow("Profundidade 2",depth2_); form->addRow("Referência Z (vértice/superfície/corpo)",target_z_); form->addRow("Offset da referência",offset_); form->addRow("Ângulo de saída",draft_);
    left_layout->addLayout(form);
    reverse_=new QCheckBox("Inverter direção",left);two_directions_=new QCheckBox("Extrudar também na direção 2",left);suppressed_=new QCheckBox("Suprimir este recurso",left);
    left_layout->addWidget(reverse_);left_layout->addWidget(two_directions_);left_layout->addWidget(suppressed_);
    auto* actions = new QHBoxLayout; auto* add = new QPushButton("+ Criar esboço e extrusão",left); add->setProperty("primary",true);auto* apply=new QPushButton("Aplicar edição",left);actions->addWidget(add);actions->addWidget(apply);left_layout->addLayout(actions);
    connect(add,&QPushButton::clicked,this,&Modeling3DWorkspace::add_feature);connect(apply,&QPushButton::clicked,this,&Modeling3DWorkspace::apply_feature);

    auto* right = new QWidget(splitter); auto* right_layout = new QVBoxLayout(right); right_layout->setContentsMargins(0,0,0,0);
    auto* viewport_heading = new QLabel("VISTA 3D DA PEÇA",right);viewport_heading->setObjectName("SectionHeader");right_layout->addWidget(viewport_heading);
    viewport_=new vp::Viewport3D(right);viewport_->setMinimumSize(480,360);right_layout->addWidget(viewport_,1);
    auto* note = new QLabel("Cada esboço e extrusão fica na árvore e pode ser reeditado. Perfis iniciais: retângulo, círculo, triângulo e hexágono.",right);note->setObjectName("MutedText");note->setWordWrap(true);right_layout->addWidget(note);
    splitter->addWidget(left);splitter->addWidget(right);splitter->setStretchFactor(0,0);splitter->setStretchFactor(1,1);splitter->setSizes({370,850});root->addWidget(splitter,1);
    connect(end_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        const auto e=static_cast<EndCondition>(end_->currentData().toInt());
        target_z_->setEnabled(e==EndCondition::UpToVertex||e==EndCondition::UpToSurface||e==EndCondition::OffsetFromSurface||e==EndCondition::UpToBody);
        offset_->setEnabled(e==EndCondition::OffsetFromSurface);
        depth_->setEnabled(e==EndCondition::Blind||e==EndCondition::MidPlane||e==EndCondition::ThroughAll||e==EndCondition::ThroughAllBoth||e==EndCondition::UpToNext);
    });
    end_->setCurrentIndex(-1);
    end_->setCurrentIndex(0);
    rebuild_tree(); rebuild_view();
}

Extrusion Modeling3DWorkspace::read_form() const {
    Extrusion f; f.profile=static_cast<Profile>(profile_->currentData().toInt()); f.width=width_->value();f.height=height_->value();f.start_z=start_z_->value();f.depth=depth_->value();f.second_depth=depth2_->value();f.target_z=target_z_->value();f.offset=offset_->value();f.draft_degrees=draft_->value();f.end=static_cast<EndCondition>(end_->currentData().toInt());f.reverse=reverse_->isChecked();f.second_direction=two_directions_->isChecked();f.suppressed=suppressed_->isChecked();return f;
}
void Modeling3DWorkspace::sync_form(const Extrusion& f) {
    profile_->setCurrentIndex(profile_->findData(int(f.profile)));width_->setValue(f.width);height_->setValue(f.height);start_z_->setValue(f.start_z);depth_->setValue(f.depth);depth2_->setValue(f.second_depth);target_z_->setValue(f.target_z);offset_->setValue(f.offset);draft_->setValue(f.draft_degrees);end_->setCurrentIndex(end_->findData(int(f.end)));reverse_->setChecked(f.reverse);two_directions_->setChecked(f.second_direction);suppressed_->setChecked(f.suppressed);
}
void Modeling3DWorkspace::add_feature() {
    auto f=read_form(); const int n=int(part_.features.size())+1; f.name="Extrusão "+std::to_string(n);part_.features.push_back(f);editing_row_=n-1;rebuild_tree();rebuild_view();
}
void Modeling3DWorkspace::select_feature(int row) { if(row<0||row>=int(part_.features.size())){editing_row_=-1;return;}editing_row_=row;sync_form(part_.features[size_t(row)]); }
void Modeling3DWorkspace::apply_feature() { if(editing_row_<0||editing_row_>=int(part_.features.size())){QMessageBox::information(this,"Modelagem 3D","Selecione uma extrusão na árvore para editar.");return;}auto f=read_form();f.name=part_.features[size_t(editing_row_)].name;part_.features[size_t(editing_row_)]=f;rebuild_tree();rebuild_view(); }
void Modeling3DWorkspace::rebuild_tree() {
    const QSignalBlocker blocker(tree_);tree_->clear();auto* root=new QTreeWidgetItem(tree_,{QString::fromStdString(part_.name)});root->setData(0,Qt::UserRole,-1);
    for(size_t i=0;i<part_.features.size();++i){const auto& f=part_.features[i];auto* sketch=new QTreeWidgetItem(root,{QString("Esboço %1 · %2").arg(i+1).arg(f.profile==Profile::Rectangle?"Retângulo":f.profile==Profile::Circle?"Círculo":f.profile==Profile::Triangle?"Triângulo":"Hexágono")});sketch->setData(0,Qt::UserRole,int(i));auto* feat=new QTreeWidgetItem(sketch,{QString::fromStdString(f.name)+(f.suppressed?" (suprimido)":"")});feat->setData(0,Qt::UserRole,int(i));sketch->setExpanded(true);}
    root->setExpanded(true);if(editing_row_>=0){auto items=tree_->findItems("Esboço *",Qt::MatchWildcard|Qt::MatchRecursive);for(auto* item:items)if(item->data(0,Qt::UserRole).toInt()==editing_row_){tree_->setCurrentItem(item);break;}}
}
void Modeling3DWorkspace::rebuild_view() {
    const auto mesh=build_mesh(part_);viewport_->clear_simulation();
    if(mesh.indices.empty()){viewport_->set_workpiece(100,60,0,18);viewport_->set_standard_view("iso");return;}
    double minx=1e100,miny=1e100,minz=1e100,maxx=-1e100,maxy=-1e100,maxz=-1e100;
    for(size_t i=0;i+5<mesh.vertices.size();i+=6){minx=std::min(minx,double(mesh.vertices[i]));maxx=std::max(maxx,double(mesh.vertices[i]));miny=std::min(miny,double(mesh.vertices[i+1]));maxy=std::max(maxy,double(mesh.vertices[i+1]));minz=std::min(minz,double(mesh.vertices[i+2]));maxz=std::max(maxz,double(mesh.vertices[i+2]));}
    viewport_->set_workpiece(std::max(1.0,maxx-minx),std::max(1.0,maxy-miny),minz,maxz);viewport_->set_simulation_mesh(mesh.vertices,mesh.indices);viewport_->set_standard_view("iso");
}
void Modeling3DWorkspace::new_part() { bool ok=false;const auto name=QInputDialog::getText(this,"Nova peça","Nome da peça:",QLineEdit::Normal,"Peça 1",&ok);if(!ok)return;part_=Part{};part_.name=name.isEmpty()?"Peça 1":name.toStdString();file_path_.clear();editing_row_=-1;rebuild_tree();rebuild_view(); }
void Modeling3DWorkspace::open_part() { const auto path=QFileDialog::getOpenFileName(this,"Abrir peça",{},"Peça CNC-CAM 3D (*.cncpart)");if(path.isEmpty())return;Part loaded;std::string error;if(!load_part(loaded,path.toStdString(),&error)){QMessageBox::critical(this,"Abrir peça",QString::fromStdString(error));return;}part_=std::move(loaded);file_path_=path;editing_row_=-1;rebuild_tree();rebuild_view(); }
void Modeling3DWorkspace::save_part_as() { const auto path=QFileDialog::getSaveFileName(this,"Salvar peça",file_path_,"Peça CNC-CAM 3D (*.cncpart)");if(path.isEmpty())return;file_path_=path;save_part(); }
void Modeling3DWorkspace::save_part() { if(file_path_.isEmpty()){save_part_as();return;}std::string error;if(!cad::model3d::save_part(part_,file_path_.toStdString(),&error))QMessageBox::critical(this,"Salvar peça",QString::fromStdString(error)); }
} // namespace cnc::ui
