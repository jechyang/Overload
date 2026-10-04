#pragma once
#include <OvEditor/Core/GizmoBehaviour.h>
#include <OvEditor/Core/GizmoMath.h>
#include <OvCore/Helpers/InspectorLabel.h>
#include <cmath>

static void EditorGizmoChecks(OvCore::SceneSystem::Scene& scene)
{
    using namespace OvMaths;
    using namespace OvEditor::Core;
    auto close=[](float a,float b) { return std::abs(a-b)<0.001f; };
    Require(close(GizmoMath::Snap(-1.6f,1),-2) && close(GizmoMath::Snap(1.6f,1),2),"gizmo snapping is symmetric around zero");
    Require(close(GizmoMath::Snap(1.5f,0),1.5f),"zero snap step cannot corrupt transforms");
    Require(OvCore::Helpers::InspectorLabel("m_MoveSpeed")=="Move Speed" && OvCore::Helpers::InspectorLabel("_URLValue")=="URL Value","Inspector labels split words and preserve acronyms");
    const FVector2 viewport{1280,720}; const FVector3 pivot{0,0,-10};
    for (float fov : {30.f,60.f,100.f}) {
        const auto projection=FMatrix4::CreatePerspective(fov,viewport.x/viewport.y,.1f,1000);
        for (float depth : {-5.f,-50.f}) {
            const FVector3 position{3,0,depth};
            const float units=GizmoMath::WorldSize(position,FMatrix4::Identity,projection,viewport.y,90);
            auto a=GizmoMath::Project(position,FMatrix4::Identity,projection,viewport);
            auto b=GizmoMath::Project(position+FVector3{0,units,0},FMatrix4::Identity,projection,viewport);
            Require(close(a.y-b.y,90),"gizmo has a stable pixel size across depth and field of view");
        }
    }
    auto& actor=scene.CreateActor("gizmo-test"); actor.transform.SetLocalPosition(pivot);
    const auto ortho=FMatrix4::CreateOrthographic(5,viewport.x/viewport.y,.1f,1000);
    GizmoBehaviour gizmo;
    const GizmoBehaviour::SnapSettings noSnap{false,1,15,.1f};
    auto apply=[&](FVector2 mouse) { gizmo.SetCurrentMouse(mouse);gizmo.ApplyOperation(FMatrix4::Identity,ortho,{},viewport,noSnap); };
    gizmo.StartPicking(actor,{},EGizmoOperation::TRANSLATE,GizmoBehaviour::EDirection::X);
    apply({640,360});apply({712,360});
    Require(close(actor.transform.GetWorldPosition().x,1),"orthographic gizmo translation uses mouse ray origin");
    gizmo.CancelPicking();
    Require(FVector3(actor.transform.GetLocalPosition())==pivot && !gizmo.IsPicking(),"cancel drag restores the original local transform");
    gizmo.StartPicking(actor,{},EGizmoOperation::SCALE,GizmoBehaviour::EDirection::X);
    apply({640,360});apply({740,360});
    Require(close(actor.transform.GetLocalScale().x,2),"scale drag has predictable pixel sensitivity");
    gizmo.CancelPicking();
    gizmo.StartPicking(actor,{},EGizmoOperation::ROTATE,GizmoBehaviour::EDirection::Z);
    apply({712,360});apply({640,288});
    const auto rotated=actor.transform.GetWorldRotation()*FVector3::Right;
    Require(close(rotated.x,0) && close(rotated.y,1),"rotation follows a quarter turn around the picked ring");
    gizmo.CancelPicking();
    gizmo.StartPicking(actor,{},EGizmoOperation::TRANSLATE,GizmoBehaviour::EDirection::Z);
    apply({640,360});apply({640,288});
    Require(close(actor.transform.GetWorldPosition().z,-9),"end-on axis translation uses a stable screen drag");
    gizmo.CancelPicking();
    gizmo.StartPicking(actor,{},EGizmoOperation::ROTATE,GizmoBehaviour::EDirection::X);
    apply({640,360});apply({640,540});
    const auto edgeRotated=actor.transform.GetWorldRotation()*FVector3::Up;
    Require(close(edgeRotated.y,0) && close(std::abs(edgeRotated.z),1),"edge-on rotation ring supports a screen-space quarter turn");
    gizmo.CancelPicking();
    const auto perspective=FMatrix4::CreatePerspective(60,viewport.x/viewport.y,.1f,1000);
    for(const auto& projection : {ortho,perspective}) {
        const GizmoBehaviour::SnapSettings snapped{true,.5f,15,.1f};
        gizmo.StartPicking(actor,{},EGizmoOperation::TRANSLATE,GizmoBehaviour::EDirection::Z);
        gizmo.SetCurrentMouse({640,360});gizmo.ApplyOperation(FMatrix4::Identity,projection,{},viewport,snapped);
        Require(FVector3::Distance(actor.transform.GetWorldPosition(),pivot)<.001f,"end-on drag starts without a jump in both projections");
        gizmo.SetCurrentMouse({640,310});gizmo.ApplyOperation(FMatrix4::Identity,projection,{},viewport,snapped);
        const float delta=actor.transform.GetWorldPosition().z-pivot.z;
        Require(delta>0 && close(delta/.5f,std::round(delta/.5f)),"screen-space translation honours snapping in both projections");
        gizmo.CancelPicking();
        gizmo.StartPicking(actor,{},EGizmoOperation::ROTATE,GizmoBehaviour::EDirection::X);
        gizmo.SetCurrentMouse({640,360});gizmo.ApplyOperation(FMatrix4::Identity,projection,{},viewport,snapped);
        gizmo.SetCurrentMouse({640,430});gizmo.ApplyOperation(FMatrix4::Identity,projection,{},viewport,snapped);
        const auto up=actor.transform.GetWorldRotation()*FVector3::Up;
        Require(close(up.y,std::cos(30.f*0.0174532925f)),"edge-on rotation honours angle snapping in both projections");
        gizmo.CancelPicking();
    }
    scene.DestroyActor(actor);scene.CollectGarbages();
}
