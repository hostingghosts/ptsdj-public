import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lib import *
from reportlab.platypus.tableofcontents import TableOfContents
import part1, part2, part3, part4

def footer(canv, doc):
    canv.saveState(); canv.setFont('Helvetica', 8.5); canv.setFillColor(colors.HexColor('#666'))
    canv.drawString(MARGIN, 0.5 * inch, 'PTSDJ Manual  -  version 1.0.3')
    canv.drawRightString(W - MARGIN, 0.5 * inch, 'page %d' % doc.page)
    canv.restoreState()

def cover(canv, doc):
    canv.saveState(); canv.setFillColor(colors.HexColor(GREEN)); canv.rect(0, H - 3.2 * inch, W, 3.2 * inch, fill=1, stroke=0)
    canv.setFillColor(colors.HexColor(PINK)); canv.rect(0, H - 3.2 * inch, W, 5, fill=1, stroke=0); canv.setFillColor(colors.HexColor(NAVY)); canv.rect(0, H - 3.2 * inch - 5, W, 5, fill=1, stroke=0)
    canv.restoreState()

class Doc(BaseDocTemplate):
    def afterFlowable(self, f):
        t = getattr(f, '_toc', None)
        if t:
            self.notify('TOCEntry', (t[0], t[1], self.page))

story = [Spacer(1, 0.55 * inch), Paragraph('PTSDJ', S['cover1']), Spacer(1, 6), Paragraph('The Manual', S['cover2']), Spacer(1, 1.9 * inch),
         fig('28-project', 'The Project page, where you save, load and export.', 3.6 * inch), Spacer(1, 10),
         Paragraph('A Game Boy-style music maker for the Polyend Tracker', S['cover2']), Spacer(1, 6),
         Paragraph('Version 1.0.3 &nbsp;-&nbsp; written for beginners. New to PTSDJ? Read the Quick Start first.', S['cover3']), PageBreak()]
toc = TableOfContents(); toc.levelStyles = [ParagraphStyle('t0', parent=base, fontName='Helvetica-Bold', fontSize=11.5, leading=17, spaceBefore=4, leftIndent=0),
                                            ParagraphStyle('t1', parent=base, fontSize=9.5, leading=12.5, spaceAfter=0, leftIndent=18, textColor=colors.HexColor('#444'))]
story += [Paragraph('Contents', S['h1']), Rule(), Spacer(1, 6), toc]
for part in (part1, part2, part3, part4): story += part.build()

def flat(x):
    for i in x:
        if isinstance(i, list): yield from flat(i)
        else: yield i

doc = Doc(__import__('os').path.join(__import__('os').path.dirname(__import__('os').path.abspath(__file__)), '..', '..', 'docs', 'PTSDJ_Manual.pdf'), pagesize=letter, leftMargin=MARGIN, rightMargin=MARGIN, topMargin=0.8 * inch, bottomMargin=0.85 * inch, title='PTSDJ Manual', author='PTSDJ')
fr = Frame(MARGIN, 0.85 * inch, BODY_W, H - 1.65 * inch, id='f')
doc.addPageTemplates([PageTemplate(id='cover', frames=[fr], onPage=cover, autoNextPageTemplate='body'), PageTemplate(id='body', frames=[fr], onPage=footer)])
doc.multiBuild(list(flat(story)))
print('built')
