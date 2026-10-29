#include "APropView.h"
#include "VRenderFrame.h"
#include <wx/valnum.h>

BEGIN_EVENT_TABLE(APropView, wxPanel)
	EVT_BUTTON(ID_MemoUpdateBtn, APropView::OnMemoUpdateBtn)
    EVT_COLOURPICKER_CHANGED(ID_diff_picker, APropView::OnDiffChange)
    EVT_COMMAND_SCROLL(ID_alpha_sldr, APropView::OnAlphaChange)
    EVT_TEXT(ID_alpha_text, APropView::OnAlphaText)
    EVT_COMMAND_SCROLL(ID_th_sldr, APropView::OnThresholdChange)
    EVT_TEXT(ID_th_text, APropView::OnThresholdText)
END_EVENT_TABLE()

APropView::APropView(wxWindow* frame, wxWindow* parent,
	wxWindowID id,
	const wxPoint& pos,
	const wxSize& size,
	long style,
	const wxString& name) :
wxPanel(parent, id, pos, size, style, name),
m_frame(frame),
m_ann(0),
m_vrv(0)
{
	SetEvtHandlerEnabled(false);
	Freeze();

	wxBoxSizer* sizer_v1 = new wxBoxSizer(wxVERTICAL);
	wxStaticText* st = 0;
    
    wxFloatingPointValidator<double> vald_fp2(2);
    wxIntegerValidator<unsigned int> vald_int;

	wxBoxSizer* sizer_1 = new wxBoxSizer(wxHORIZONTAL);
	st = new wxStaticText(this, 0, "Memo:",
		wxDefaultPosition, wxDefaultSize);
	sizer_1->Add(10, 10);
	sizer_1->Add(st);

	wxBoxSizer* sizer_2 = new wxBoxSizer(wxHORIZONTAL);
	m_memo_text = new wxTextCtrl(this, ID_MemoText, "",
		wxDefaultPosition, wxSize(400, 100), wxTE_MULTILINE);
	sizer_2->Add(10, 10);
	sizer_2->Add(m_memo_text, 1, wxEXPAND);

	wxBoxSizer* sizer_3 = new wxBoxSizer(wxHORIZONTAL);
	m_memo_update_btn = new wxButton(this, ID_MemoUpdateBtn, "Update");
	sizer_3->Add(330, 10);
	sizer_3->Add(m_memo_update_btn, 0, wxALIGN_CENTER);

	sizer_v1->Add(sizer_1, 0, wxALIGN_LEFT);
	sizer_v1->Add(sizer_2, 1, wxALIGN_LEFT);
	sizer_v1->Add(sizer_3, 0, wxALIGN_LEFT);
    
    wxBoxSizer* sizer_v2 = new wxBoxSizer(wxVERTICAL);
    
    wxBoxSizer* sizer_4 = new wxBoxSizer(wxHORIZONTAL);
    st = new wxStaticText(this, 0, " Transparency: ", wxDefaultPosition, wxSize(100, 20));
    m_alpha_sldr = new wxSlider(this, ID_alpha_sldr, 255, 0, 255, wxDefaultPosition, wxSize(200, 20), wxSL_HORIZONTAL);
    m_alpha_text = new wxTextCtrl(this, ID_alpha_text, "1.00", wxDefaultPosition, wxSize(50, 20), 0, vald_fp2);
    sizer_4->Add(20, 5, 0);
    sizer_4->Add(st, 0, wxALIGN_CENTER, 0);
    sizer_4->Add(m_alpha_sldr, 0, wxALIGN_CENTER, 0);
    sizer_4->Add(m_alpha_text, 0, wxALIGN_CENTER, 0);
    
    wxBoxSizer* sizer_5 = new wxBoxSizer(wxHORIZONTAL);
    st = new wxStaticText(this, 0, " Min Score: ", wxDefaultPosition, wxSize(100, 20));
    m_th_sldr = new wxSlider(this, ID_th_sldr, 0, 0, 65535, wxDefaultPosition, wxSize(200, 20), wxSL_HORIZONTAL);
    m_th_text = new wxTextCtrl(this, ID_th_text, "0", wxDefaultPosition, wxSize(50, 20), 0, vald_int);
    sizer_5->Add(20, 5, 0);
    sizer_5->Add(st, 0, wxALIGN_CENTER, 0);
    sizer_5->Add(m_th_sldr, 0, wxALIGN_CENTER, 0);
    sizer_5->Add(m_th_text, 0, wxALIGN_CENTER, 0);
    
    m_num_particles = new wxStaticText(this, 0, "        Number of Particles:  ", wxDefaultPosition, wxSize(250, 20));
    
    wxBoxSizer* sizer_6 = new wxBoxSizer(wxHORIZONTAL);
    st = new wxStaticText(this, 0, " Color: ",
        wxDefaultPosition, wxSize(110, 20));
    m_diff_picker = new wxColourPickerCtrl(this, ID_diff_picker, *wxWHITE,
        wxDefaultPosition, wxSize(180, 30));
    sizer_6->Add(st, 0, wxALIGN_LEFT, 0);
    sizer_6->Add(m_diff_picker, 0, wxALIGN_CENTER, 0);
    
    sizer_v2->Add(5,5);
    sizer_v2->Add(sizer_4, 0, wxALIGN_LEFT);
    sizer_v2->Add(5,5);
    sizer_v2->Add(sizer_5, 0, wxALIGN_LEFT);
    sizer_v2->Add(5,5);
    sizer_v2->Add(m_num_particles, 0, wxALIGN_LEFT);
    sizer_v2->Add(5,5);
    sizer_v2->Add(sizer_6, 0, wxALIGN_LEFT);
    
    wxBoxSizer* sizer_all = new wxBoxSizer(wxHORIZONTAL);
    sizer_all->Add(sizer_v1, 0, wxALIGN_TOP);
    sizer_all->Add(sizer_v2, 0, wxALIGN_TOP);
    
    SetSizer(sizer_all);
    
	Layout();

	Thaw();
	SetEvtHandlerEnabled(true);
}

APropView::~APropView()
{
}

void APropView::GetSettings()
{
	if (!m_ann)
		return;

	// the engine keeps memos as UTF-8 (Annotations::Load / Save, the Qt panel)
	wxString memo = wxString::FromUTF8(m_ann->GetMemo());
	m_memo_text->SetValue(memo);
	if (m_ann->GetMemoRO())
	{
		m_memo_text->SetEditable(false);
		m_memo_update_btn->Disable();
	}
	else
	{
		m_memo_text->SetEditable(true);
		m_memo_update_btn->Enable();
	}
    
    if (m_ann->GetMesh())
    {
        string str;
        Color amb, diff, spec;
        double shine, alpha, th;
        m_ann->GetMesh()->GetMaterial(amb, diff, spec, shine, alpha);

        wxColor c;
        c = wxColor(diff.r()*255, diff.g()*255, diff.b()*255);
        m_diff_picker->SetColour(c);
        Color color(c.Red()/255.0, c.Green()/255.0, c.Blue()/255.0);
        m_ann->GetMesh()->SetColor(color, MESH_COLOR_DIFF);
        amb = color * 0.3;
        m_ann->GetMesh()->SetColor(amb, MESH_COLOR_AMB);
        
        //alpha
        alpha = m_ann->GetAlpha();
        m_alpha_sldr->SetValue(int(alpha*255));
        str = wxString::Format("%.2f", alpha);
        m_alpha_text->ChangeValue(str);
        
        //threshold
        m_th_sldr->SetMax((int)(m_ann->GetMaxScore() + 1.0));
        th = m_ann->GetThreshold();
        m_th_sldr->SetValue(int(th + 0.5));
        str = wxString::Format("%d", (int)(th + 0.5));
        m_th_text->ChangeValue(str);
        
        m_diff_picker->Show();
        m_alpha_sldr->Show();
        m_alpha_text->Show();
        m_th_sldr->Show();
        m_th_text->Show();
        m_num_particles->Show();
        
        wxCommandEvent event;
        OnThresholdText(event);
        m_num_particles->GetParent()->Layout();
    }
    else
    {
        m_diff_picker->Hide();
        m_alpha_sldr->Hide();
        m_alpha_text->Hide();
        m_th_sldr->Hide();
        m_th_text->Hide();
        m_num_particles->Hide();
    }
}

void APropView::SetAnnotations(Annotations* ann, VRenderView* vrv)
{
	m_ann = ann;
	m_vrv = vrv;

	GetSettings();
}

Annotations* APropView::GetAnnotations()
{
	return m_ann;
}

void APropView::RefreshVRenderViews(bool tree)
{
	VRenderFrame* vrender_frame = (VRenderFrame*)m_frame;
	if (vrender_frame)
		vrender_frame->RefreshVRenderViews(tree);
}

void APropView::OnMemoUpdateBtn(wxCommandEvent& event)
{
	if (m_ann)
	{
		wxString memo = m_memo_text->GetValue();
		std::string str(memo.utf8_str());   // UTF-8, as the engine stores memos
		m_ann->SetMemo(str);
	}
}

void APropView::OnAlphaChange(wxScrollEvent & event)
{
    double val = (double)event.GetPosition() / 255.0;
    wxString str = wxString::Format("%.2f", val);
    m_alpha_text->SetValue(str);
}

void APropView::OnAlphaText(wxCommandEvent& event)
{
    wxString str = m_alpha_text->GetValue();
    double alpha;
    str.ToDouble(&alpha);
    m_alpha_sldr->SetValue(int(alpha*255.0+0.5));

    if (m_ann)
    {
        m_ann->SetAlpha(alpha);
        RefreshVRenderViews();
    }
}

void APropView::OnThresholdChange(wxScrollEvent & event)
{
    double val = (double)event.GetPosition();
    wxString str = wxString::Format("%d", (int)(val + 0.5));
    m_th_text->SetValue(str);
}

void APropView::OnThresholdText(wxCommandEvent& event)
{
    wxString str = m_th_text->GetValue();
    double th;
    str.ToDouble(&th);
    m_th_sldr->SetValue(int(th+0.5));
    
    long count = 0;
    if (m_ann)
    {
        VRenderFrame* vrender_frame = (VRenderFrame*)m_frame;
        if (vrender_frame)
        {
            MeasureDlg* mdlg = vrender_frame->GetMeasureDlg();
            if (mdlg)
            {
                mdlg->UpdateList();
                count = mdlg->GetCount(m_ann);
            }
        }
        
        m_num_particles->SetLabelText(wxString::Format("       Number of Particles:  %ld", count));
        m_num_particles->GetParent()->Layout();
        
        m_ann->SetThreshold(th);
        RefreshVRenderViews();
    }
}

void APropView::OnDiffChange(wxColourPickerEvent& event)
{
    wxColor c = event.GetColour();
    Color color(c.Red()/255.0, c.Green()/255.0, c.Blue()/255.0);
    if (m_ann && m_ann->GetMesh())
    {
        m_ann->GetMesh()->SetColor(color, MESH_COLOR_DIFF);
        Color amb = color * 0.3;
        m_ann->GetMesh()->SetColor(amb, MESH_COLOR_AMB);
        RefreshVRenderViews(true);
    }
}
