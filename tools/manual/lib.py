"""Shared pieces for the PTSDJ guides: styles, screenshots, callouts, the key map and the page map."""
import os
from PIL import Image
from reportlab.lib import colors
from reportlab.lib.enums import TA_LEFT, TA_CENTER
from reportlab.lib.pagesizes import letter
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.units import inch
from reportlab.platypus import (BaseDocTemplate, Flowable, Frame, Image as RLImage, KeepTogether, PageBreak,
                                PageTemplate, Paragraph, Spacer, Table, TableStyle, ListFlowable, ListItem, CondPageBreak)

HERE = os.path.dirname(os.path.abspath(__file__))
SHOTS = os.path.join(HERE, '..', '..', 'build', 'manual', 'shots')
CROPS = os.path.join(HERE, '..', '..', 'build', 'manual', 'crops')
os.makedirs(CROPS, exist_ok=True)

GREEN, DARK, PINK, NAVY, PALE = '#8bac0f', '#2b3d12', '#8E2F5A', '#2D3258', '#eef3dc'
W, H = letter
MARGIN = 0.85 * inch
BODY_W = W - 2 * MARGIN

base = ParagraphStyle('base', fontName='Helvetica', fontSize=11, leading=16, spaceAfter=7, textColor=colors.HexColor('#1d1d1d'))
S = {
    'body': base,
    'small': ParagraphStyle('small', parent=base, fontSize=9.5, leading=13, spaceAfter=4),
    'cap': ParagraphStyle('cap', parent=base, fontSize=9, leading=12, textColor=colors.HexColor('#555555'), alignment=TA_CENTER, spaceBefore=3, spaceAfter=10),
    'h1': ParagraphStyle('h1', parent=base, fontName='Helvetica-Bold', fontSize=26, leading=30, textColor=colors.HexColor(DARK), spaceAfter=10, spaceBefore=0),
    'h2': ParagraphStyle('h2', parent=base, fontName='Helvetica-Bold', fontSize=16, leading=20, textColor=colors.HexColor(NAVY), spaceBefore=14, spaceAfter=6, keepWithNext=1),
    'h3': ParagraphStyle('h3', parent=base, fontName='Helvetica-Bold', fontSize=12.5, leading=16, textColor=colors.HexColor(PINK), spaceBefore=8, spaceAfter=4, keepWithNext=1),
    'cell': ParagraphStyle('cell', parent=base, fontSize=9.8, leading=13, spaceAfter=0),
    'cellb': ParagraphStyle('cellb', parent=base, fontName='Helvetica-Bold', fontSize=9.8, leading=13, spaceAfter=0),
    'cover1': ParagraphStyle('cover1', parent=base, fontName='Helvetica-Bold', fontSize=54, leading=58, alignment=TA_CENTER, textColor=colors.HexColor(DARK)),
    'cover2': ParagraphStyle('cover2', parent=base, fontSize=17, leading=23, alignment=TA_CENTER, textColor=colors.HexColor(NAVY)),
    'cover3': ParagraphStyle('cover3', parent=base, fontSize=11, leading=15, alignment=TA_CENTER, textColor=colors.HexColor('#555555')),
}

def K(t):
    """A key or button name."""
    return '<font name="Helvetica-Bold" color="%s">%s</font>' % (NAVY, t)

def keys(s):
    """Marks every [Name] in s as a key."""
    out, i = '', 0
    while True:
        a = s.find('[', i)
        if a < 0: return out + s[i:]
        b = s.find(']', a)
        out += s[i:a] + K(s[a + 1:b]); i = b + 1

def P(t, style='body'): return Paragraph(keys(t), S[style])
class TP(Paragraph):
    pass

def H1(t):
    p = TP(t, S['h1']); p._toc = (0, t)
    return [PageBreak(), p, Rule(), Spacer(1, 8)]

def H2(t, toc=True):
    p = TP(t, S['h2'])
    if toc: p._toc = (1, t)
    return p
def H3(t): return Paragraph(t, S['h3'])

class Rule(Flowable):
    def wrap(self, w, h): self.w = w; return w, 6
    def draw(self):
        self.canv.setStrokeColor(colors.HexColor(GREEN)); self.canv.setLineWidth(3); self.canv.line(0, 2, self.w, 2)

def bullets(items, style='body'):
    return ListFlowable([ListItem(P(t, style), leftIndent=14) for t in items], bulletType='bullet', start='•', leftIndent=14, bulletFontSize=11)

def steps(items, start=1):
    return ListFlowable([ListItem(P(t), leftIndent=22) for t in items], bulletType='1', start=start, leftIndent=22, bulletFontName='Helvetica-Bold', bulletColor=colors.HexColor(PINK))

def callout(kind, text):
    col, label = {'tip': ('#e3efc5', 'TIP'), 'care': ('#f7d9d9', 'CAREFUL'), 'note': ('#dbe4f5', 'GOOD TO KNOW')}[kind]
    edge = {'tip': '#6c8a12', 'care': '#b33a3a', 'note': NAVY}[kind]
    inner = [Paragraph('<font name="Helvetica-Bold" color="%s" size="9">%s</font>' % (edge, label), S['small']), P(text, 'small') if isinstance(text, str) else text]
    t = Table([[inner]], colWidths=[BODY_W])
    t.setStyle(TableStyle([('BACKGROUND', (0, 0), (-1, -1), colors.HexColor(col)), ('LINEBEFORE', (0, 0), (0, -1), 4, colors.HexColor(edge)),
                           ('LEFTPADDING', (0, 0), (-1, -1), 10), ('RIGHTPADDING', (0, 0), (-1, -1), 10), ('TOPPADDING', (0, 0), (-1, -1), 6), ('BOTTOMPADDING', (0, 0), (-1, -1), 4)]))
    return [t, Spacer(1, 8)]

def table(rows, widths, head=True):
    data = [[Paragraph(keys(c) if isinstance(c, str) else c, S['cellb'] if (head and i == 0) else S['cell']) for c in r] for i, r in enumerate(rows)]
    t = Table(data, colWidths=widths, repeatRows=1 if head else 0)
    st = [('VALIGN', (0, 0), (-1, -1), 'TOP'), ('GRID', (0, 0), (-1, -1), 0.4, colors.HexColor('#b9c79a')), ('LEFTPADDING', (0, 0), (-1, -1), 6), ('RIGHTPADDING', (0, 0), (-1, -1), 6),
          ('TOPPADDING', (0, 0), (-1, -1), 4), ('BOTTOMPADDING', (0, 0), (-1, -1), 4)]
    if head: st.append(('BACKGROUND', (0, 0), (-1, 0), colors.HexColor('#d8e5a8')))
    for i in range(1, len(rows)):
        if i % 2 == 0: st.append(('BACKGROUND', (0, i), (-1, i), colors.HexColor('#f4f8e6')))
    t.setStyle(TableStyle(st))
    return [t, Spacer(1, 8)]

def crop(name, box=(92, 20, 708, 468)):
    src = os.path.join(SHOTS, name + '.png')
    dst = os.path.join(CROPS, name + '.png')
    im = Image.open(src).convert('RGB').crop(box)
    im.save(dst)
    return dst, im.size

def fig(name, caption, width=4.2 * inch, box=(92, 20, 708, 468)):
    path, (w, h) = crop(name, box)
    img = RLImage(path, width=width, height=width * h / w)
    t = Table([[img]], colWidths=[width + 8])
    t.setStyle(TableStyle([('BOX', (0, 0), (-1, -1), 2.5, colors.HexColor('#3b3f4a')), ('LEFTPADDING', (0, 0), (-1, -1), 2), ('RIGHTPADDING', (0, 0), (-1, -1), 2), ('TOPPADDING', (0, 0), (-1, -1), 2), ('BOTTOMPADDING', (0, 0), (-1, -1), 2)]))
    t.hAlign = 'CENTER'
    return KeepTogether([t, Paragraph(keys(caption), S['cap'])])

def fig2(n1, c1, n2, c2, width=3.05 * inch):
    cells = []
    for n, c in ((n1, c1), (n2, c2)):
        path, (w, h) = crop(n)
        img = RLImage(path, width=width, height=width * h / w)
        inner = Table([[img]], colWidths=[width + 6])
        inner.setStyle(TableStyle([('BOX', (0, 0), (-1, -1), 2, colors.HexColor('#3b3f4a')), ('LEFTPADDING', (0, 0), (-1, -1), 2), ('RIGHTPADDING', (0, 0), (-1, -1), 2), ('TOPPADDING', (0, 0), (-1, -1), 2), ('BOTTOMPADDING', (0, 0), (-1, -1), 2)]))
        cells.append([inner, Paragraph(keys(c), S['cap'])])
    t = Table([[cells[0], cells[1]]], colWidths=[BODY_W / 2] * 2)
    t.setStyle(TableStyle([('ALIGN', (0, 0), (-1, -1), 'CENTER'), ('VALIGN', (0, 0), (-1, -1), 'TOP')]))
    return KeepTogether([t])

# ---- the Tracker's keys ------------------------------------------------------------------------------------------------------------
class KeyMap(Flowable):
    """The Polyend Tracker's face as it really is: screen and pads on the left, keys and jog wheel on the right."""
    GROUPS = {'page': '#cfd9f2', 'sample': '#d5ead0', 'edit': '#fbe5b8', 'play': '#f4c4c4', 'other': '#e6e6e6',
              'note': '#7fe0cc', 'instr': '#e8e49a', 'fx1': '#c99de0', 'fx2': '#7fd3df'}
    def __init__(self, width): super().__init__(); self.width = width
    def wrap(self, w, h): self.height = 4.5 * inch; return self.width, self.height
    def draw(self):
        c = self.canv
        def box(x, y, w, h, label, grp='other', size=6.6, sub=None):
            c.setFillColor(colors.HexColor(self.GROUPS[grp])); c.setStrokeColor(colors.HexColor('#444444')); c.setLineWidth(0.7)
            c.roundRect(x, y, w, h, 3, fill=1, stroke=1)
            c.setFillColor(colors.HexColor('#1d1d1d')); c.setFont('Helvetica-Bold', size)
            lines = label.split('\n')
            for i, l in enumerate(lines):
                c.drawCentredString(x + w / 2, y + h / 2 + (len(lines) - 1) * 3.8 - i * 8 - 2.4, l)
            if sub:
                c.setFont('Helvetica', 5.8); c.setFillColor(colors.HexColor('#555555')); c.drawCentredString(x + w / 2, y + h + 1.5, sub)
        top = self.height
        sw = 292
        # screen
        c.setFillColor(colors.HexColor('#25272e')); c.roundRect(0, top - 140, sw, 140, 5, fill=1, stroke=0)
        c.setFillColor(colors.HexColor(GREEN)); c.rect(8, top - 132, sw - 16, 124, fill=1, stroke=0)
        c.setFillColor(colors.HexColor(DARK)); c.setFont('Helvetica-Bold', 9); c.drawCentredString(sw / 2, top - 74, 'the screen')
        # red light (on the frame of the screen picture)
        c.setFillColor(colors.HexColor('#cc3333')); c.rect(-9, top - 86, 6, 6, fill=1, stroke=0)
        # F keys
        for i in range(8): box(i * 36.6, top - 172, 31, 22, 'F%d' % (i + 1), 'other', 7)
        # pads
        for r in range(4):
            for q in range(12):
                c.setFillColor(colors.HexColor('#e9e9e9')); c.setStrokeColor(colors.HexColor('#999999')); c.roundRect(q * 24.4, top - 205 - r * 22, 22, 19, 2, fill=1, stroke=1)
        c.setFillColor(colors.HexColor('#444')); c.setFont('Helvetica', 7.5); c.drawString(0, top - 298, '48 pads: they play sounds, or type notes')
        # the key grid (right side)
        gx, col = 318, 36
        rows = [[('Instrument\nParameters', 'page'), ('Perform', 'page'), ('File', 'page'), ('Config', 'page'), ('Master', 'page')],
                [('Sample\nLoader', 'sample'), ('Sample\nPlayback', 'sample'), ('Sample\nEditor', 'sample'), ('Sample\nRecorder', 'sample'), ('Song', 'page')],
                [('Note', 'note'), ('Instrument', 'instr'), ('FX1', 'fx1'), ('FX2', 'fx2'), ('Pattern', 'page')]]
        for ri, row in enumerate(rows):
            for ci, cell in enumerate(row):
                box(gx + ci * col, top - 30 - ri * 31, 33, 26, cell[0], cell[1], 5.4 if '\n' in cell[0] or len(cell[0]) > 8 else 6.4)
        lower = [[('Play', 'play', None), ('Copy', 'edit', 'paste'), ('Insert', 'edit', 'home'), ('Up', 'edit', None), ('Delete', 'edit', 'backspace')],
                 [('Rec', 'play', None), ('Shift', 'edit', None), ('Left', 'edit', None), ('Down', 'edit', None), ('Right', 'edit', None)]]
        for ri, row in enumerate(lower):
            for ci, (lab, grp, sub) in enumerate(row):
                box(gx + ci * col, top - 150 - ri * 31, 33, 25, lab, grp, 6.4, sub)
        # jog wheel
        cx, cy = gx + 2 * col + 17, top - 232
        c.setFillColor(colors.HexColor('#2b2d33')); c.setStrokeColor(colors.HexColor('#111111')); c.circle(cx, cy, 33, fill=1, stroke=1)
        c.setFillColor(colors.HexColor('#555a66')); c.circle(cx, cy, 11, fill=1, stroke=0)
        c.setFillColor(colors.HexColor('#1d1d1d')); c.setFont('Helvetica-Bold', 7.5); c.drawCentredString(cx, cy - 46, 'jog wheel')
        # legend
        lx, ly = 0, 0
        for i, (g, t) in enumerate((('page', 'go to a page'), ('sample', 'sample keys'), ('edit', 'edit and move'), ('play', 'play and record'))):
            c.setFillColor(colors.HexColor(self.GROUPS[g])); c.setStrokeColor(colors.HexColor('#444')); c.rect(lx + i * 98, ly, 9, 9, fill=1, stroke=1)
            c.setFillColor(colors.HexColor('#1d1d1d')); c.setFont('Helvetica', 7.5); c.drawString(lx + i * 98 + 13, ly + 1.5, t)

class PageMap(Flowable):
    """How the pages sit next to each other (Shift + arrows move between them)."""
    def __init__(self, width): super().__init__(); self.width = width
    def wrap(self, w, h): self.height = 2.0 * inch; return self.width, self.height
    def draw(self):
        c = self.canv
        bw, bh, gap = 72, 30, 12
        x0 = (self.width - (5 * bw + 4 * gap)) / 2
        def box(col, row, label, grp):
            x = x0 + col * (bw + gap); y = self.height - 38 - row * 56
            c.setFillColor(colors.HexColor({'a': '#d8e5a8', 'b': '#cfd9f2', 'c': '#fbe5b8'}[grp])); c.setStrokeColor(colors.HexColor('#555')); c.setLineWidth(0.8)
            c.roundRect(x, y, bw, bh, 4, fill=1, stroke=1)
            c.setFillColor(colors.HexColor('#1d1d1d')); c.setFont('Helvetica-Bold', 9); c.drawCentredString(x + bw / 2, y + 11, label)
        for col, lab in enumerate(('Song', 'Chain', 'Phrase', 'Instr', 'Table')): box(col, 1, lab, 'a')
        box(0.5, 0, 'Project', 'b'); box(2, 0, 'Synth', 'b'); box(3.5, 0, 'Wave', 'b'); box(2, 2, 'Groove', 'c')
        c.setFillColor(colors.HexColor('#444')); c.setFont('Helvetica', 8)
        c.drawString(4, 4, 'Shift + Left / Right / Up / Down walks from page to page.')
