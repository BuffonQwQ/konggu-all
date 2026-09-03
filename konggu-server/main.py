from flask import Flask, request
import os

app = Flask(__name__)

# 确保有一个接收文件的路由
@app.route('/upload', methods=['POST'])
def upload_file():
    if 'file' not in request.files:
        return 'No file part', 400
    file = request.files['file']
    if file.filename == '':
        return 'No selected file', 400
    # 保存文件
    sav_p=os.path.join('uploads')
    os.makedirs(sav_p, exist_ok=True)
    file.save(os.path.join(sav_p, file.filename))
    return {'result':1}, 200, {"Content-Type": "application/json"}

if __name__ == '__main__':
    app.run(debug=True, host='localhost', port=1234)

