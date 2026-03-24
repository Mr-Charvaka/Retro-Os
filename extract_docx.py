
import zipfile
import xml.etree.ElementTree as ET
import sys

def get_docx_text(path):
    try:
        with zipfile.ZipFile(path, 'r') as docx:
            xml_content = docx.read('word/document.xml')
            tree = ET.fromstring(xml_content)
            
            ns = {'w': 'http://schemas.openxmlformats.org/wordprocessingml/2006/main'}
            
            texts = []
            for p in tree.findall('.//w:p', ns):
                paragraph_text = []
                for t in p.findall('.//w:t', ns):
                    if t.text:
                        paragraph_text.append(t.text)
                if paragraph_text:
                    texts.append("".join(paragraph_text))
            
            return "\n".join(texts)
    except Exception as e:
        return f"Error: {str(e)}"

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python extract_docx.py <path_to_docx>")
    else:
        text = get_docx_text(sys.argv[1])
        with open("analysis_extracted.txt", "w", encoding="utf-8") as f:
            f.write(text)
        print("Done. Saved to analysis_extracted.txt")
