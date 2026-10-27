#include "SkinGeometry.h"
#include <QVector3D>
#include <QVector2D>
void SkinGeometry::rebuild(){
    float w=8,h=8,d=8,u=0,v=0;
    switch(m_part){case 1:w=8;h=12;d=4;u=16;v=16;break;case 2:case 3:w=m_slim?3:4;h=12;d=4;u=(m_part==3&&!m_legacy)?32:40;v=(m_part==3&&!m_legacy)?48:16;break;case 4:case 5:w=4;h=12;d=4;u=(m_part==5&&!m_legacy)?16:0;v=(m_part==5&&!m_legacy)?48:16;break;case 6:u=32;break;case 7:w=8;h=12;d=4;u=16;v=32;break;case 8:case 9:w=m_slim?3:4;h=12;d=4;u=m_part==8?40:48;v=m_part==8?32:48;break;case 10:case 11:w=4;h=12;d=4;u=0;v=m_part==10?32:48;break;default:break;}
    struct Vertex{float px,py,pz,nx,ny,nz,u,v;};QVector<Vertex> vertices;
    auto face=[&](QVector3D a,QVector3D b,QVector3D c,QVector3D e,QVector3D n,float x,float y,float fw,float fh){QVector3D points[4]{a,b,c,e};QVector2D uv[4]{{x,y+fh},{x+fw,y+fh},{x+fw,y},{x,y}};for(int i:{0,1,2,0,2,3}){auto p=points[i];vertices.append({p.x(),p.y(),p.z(),n.x(),n.y(),n.z(),uv[i].x()/64.f,uv[i].y()/(m_legacy?32.f:64.f)});}};
    float x=w/2,y=h/2,z=d/2;
    face({-x,-y,z},{x,-y,z},{x,y,z},{-x,y,z},{0,0,1},u+d,v+d,w,h);
    face({x,-y,-z},{-x,-y,-z},{-x,y,-z},{x,y,-z},{0,0,-1},u+2*d+w,v+d,w,h);
    face({-x,-y,-z},{-x,-y,z},{-x,y,z},{-x,y,-z},{-1,0,0},u,v+d,d,h);
    face({x,-y,z},{x,-y,-z},{x,y,-z},{x,y,z},{1,0,0},u+d+w,v+d,d,h);
    face({-x,y,z},{x,y,z},{x,y,-z},{-x,y,-z},{0,1,0},u+d,v,w,d);
    face({-x,-y,-z},{x,-y,-z},{x,-y,z},{-x,-y,z},{0,-1,0},u+d+w,v,w,d);
    clear();setStride(sizeof(Vertex));setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic,0,QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::NormalSemantic,3*sizeof(float),QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::TexCoordSemantic,6*sizeof(float),QQuick3DGeometry::Attribute::F32Type);
    setVertexData(QByteArray(reinterpret_cast<const char*>(vertices.constData()),vertices.size()*sizeof(Vertex)));setBounds({-x,-y,-z},{x,y,z});update();
}
