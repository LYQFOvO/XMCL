#pragma once
#include <QQuick3DGeometry>
class SkinGeometry:public QQuick3DGeometry {
    Q_OBJECT
    Q_PROPERTY(int part READ part WRITE setPart NOTIFY changed)
    Q_PROPERTY(bool slim READ slim WRITE setSlim NOTIFY changed)
    Q_PROPERTY(bool legacy READ legacy WRITE setLegacy NOTIFY changed)
public:
    explicit SkinGeometry(QQuick3DObject*parent=nullptr):QQuick3DGeometry(parent){rebuild();}
    int part()const{return m_part;}bool slim()const{return m_slim;}bool legacy()const{return m_legacy;}
    void setPart(int v){if(m_part!=v){m_part=v;rebuild();emit changed();}}
    void setSlim(bool v){if(m_slim!=v){m_slim=v;rebuild();emit changed();}}
    void setLegacy(bool v){if(m_legacy!=v){m_legacy=v;rebuild();emit changed();}}
signals:void changed();
private:
    int m_part=0;bool m_slim=false,m_legacy=false;void rebuild();
};
